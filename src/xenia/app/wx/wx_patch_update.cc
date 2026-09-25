/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// Game patch updater: lists *.patch.toml upstream, downloads each file, and
// merges it over the local copy while preserving the user's is_enabled
// choices (matched by patch name, positionally for unnamed entries).

#include "xenia/app/wx/wx_patch_update.h"

#include <chrono>
#include <fstream>
#include <map>
#include <sstream>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/gauge.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/timer.h>

#include "rapidjson/document.h"

#include "xenia/app/wx/wx_compat_db.h"
#include "xenia/app/wx/wx_util.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/patcher/patch_db.h"

namespace xe {
namespace app {
namespace wx_ui {
namespace {

constexpr std::string_view kPatchesApiUrl =
    "https://api.github.com/repos/xenia-canary/game-patches/contents/"
    "patches";
constexpr std::string_view kPatchSuffix = ".patch.toml";

struct RemoteFile {
  std::string name;
  std::string download_url;
};

// Percent-encodes bytes outside the URL-allowed set, leaving existing
// %XX escapes intact. GitHub pre-encodes ASCII (spaces etc.) but leaves
// non-ASCII UTF-8 bytes raw, which libcurl will not encode itself.
std::string EncodeUrl(const std::string& url) {
  auto allowed = [](unsigned char c) {
    if (c <= 0x20 || c >= 0x7F) {
      return false;
    }
    switch (c) {
      case '<':
      case '>':
      case '"':
      case '{':
      case '}':
      case '|':
      case '\\':
      case '^':
      case '`':
        return false;
      default:
        return true;
    }
  };
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  out.reserve(url.size());
  for (unsigned char c : url) {
    if (allowed(c)) {
      out += char(c);
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}

// One GitHub API call for the whole directory listing.
bool ListRemotePatches(std::vector<RemoteFile>* out) {
  std::string body;
  if (!HttpGet(std::string(kPatchesApiUrl), &body)) {
    return false;
  }
  rapidjson::Document doc;
  doc.Parse(body.data(), body.size());
  if (doc.HasParseError() || !doc.IsArray()) {
    return false;
  }
  for (const auto& entry : doc.GetArray()) {
    if (!entry.IsObject()) {
      continue;
    }
    const auto type_it = entry.FindMember("type");
    const auto name_it = entry.FindMember("name");
    const auto url_it = entry.FindMember("download_url");
    if (type_it == entry.MemberEnd() || name_it == entry.MemberEnd() ||
        url_it == entry.MemberEnd()) {
      continue;
    }
    if (!type_it->value.IsString() || !name_it->value.IsString() ||
        !url_it->value.IsString()) {
      continue;
    }
    if (std::string_view(type_it->value.GetString()) != "file") {
      continue;
    }
    std::string name(name_it->value.GetString(),
                     name_it->value.GetStringLength());
    if (name.size() <= kPatchSuffix.size() ||
        name.substr(name.size() - kPatchSuffix.size()) != kPatchSuffix) {
      continue;
    }
    // Flat directory: reject anything that could escape it.
    if (name.find('/') != std::string::npos ||
        name.find('\\') != std::string::npos) {
      continue;
    }
    out->push_back(
        {std::move(name), std::string(url_it->value.GetString(),
                                      url_it->value.GetStringLength())});
  }
  return true;
}

bool ReadAllBytes(const std::filesystem::path& path, std::string* out) {
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    return false;
  }
  std::ostringstream contents;
  contents << input.rdbuf();
  if (input.bad()) {
    return false;
  }
  *out = contents.str();
  return true;
}

bool WriteAllBytes(const std::filesystem::path& path, const std::string& data) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    return false;
  }
  output.write(data.data(), static_cast<std::streamsize>(data.size()));
  output.close();
  return !output.fail();
}

// Carries the local file's is_enabled states into the already-written new
// content. Returns {ok, preserved}: preserved counts the local states that
// found a new home, whether or not they differed.
std::pair<bool, size_t> MergeLocalEnabledFlags(
    const patcher::PatchFileEntry& local,
    const std::filesystem::path& new_path) {
  patcher::PatchFileEntry fresh = patcher::PatchDB::ReadPatchFile(new_path);
  if (fresh.title_id == static_cast<uint32_t>(-1)) {
    return {false, 0};
  }
  std::map<std::string, std::vector<bool>> local_by_name;
  std::vector<bool> local_unnamed;
  for (const auto& e : local.patch_info) {
    if (e.patch_name.empty()) {
      local_unnamed.push_back(e.is_enabled);
    } else {
      local_by_name[e.patch_name].push_back(e.is_enabled);
    }
  }
  std::map<std::string, size_t> consumed;
  size_t unnamed_idx = 0;
  size_t preserved = 0;
  std::vector<std::pair<size_t, bool>> desired;
  for (size_t i = 0; i < fresh.patch_info.size(); ++i) {
    const auto& entry = fresh.patch_info[i];
    bool found = false;
    bool local_flag = false;
    if (!entry.patch_name.empty()) {
      const auto it = local_by_name.find(entry.patch_name);
      if (it != local_by_name.end() &&
          consumed[entry.patch_name] < it->second.size()) {
        local_flag = it->second[consumed[entry.patch_name]++];
        found = true;
      }
    } else if (unnamed_idx < local_unnamed.size()) {
      local_flag = local_unnamed[unnamed_idx++];
      found = true;
    }
    if (!found) {
      continue;
    }
    ++preserved;
    if (local_flag != entry.is_enabled) {
      desired.emplace_back(i, local_flag);
    }
  }
  if (!patcher::PatchDB::WriteEnabledFlags(new_path, desired,
                                           /*make_backup=*/false)) {
    return {false, 0};
  }
  // Re-parse to confirm the flags took.
  fresh = patcher::PatchDB::ReadPatchFile(new_path);
  for (const auto& [index, enabled] : desired) {
    if (index >= fresh.patch_info.size() ||
        fresh.patch_info[index].is_enabled != enabled) {
      return {false, 0};
    }
  }
  return {true, preserved};
}

void UpdateOneFile(const std::filesystem::path& patches_dir,
                   const RemoteFile& remote, const std::string& bytes,
                   const std::shared_ptr<PatchUpdateProgress>& progress) {
  const auto path = patches_dir / xe::to_path(remote.name);
  std::error_code ec;
  const bool local_exists = std::filesystem::exists(path, ec);
  patcher::PatchFileEntry local;
  if (local_exists) {
    std::string local_bytes;
    if (ReadAllBytes(path, &local_bytes) && local_bytes == bytes) {
      std::lock_guard<std::mutex> lock(progress->mutex);
      ++progress->unchanged;
      return;
    }
    local = patcher::PatchDB::ReadPatchFile(path);
    // Keep the pre-update version next to the file before overwriting.
    std::filesystem::copy_file(
        path, std::filesystem::path(path.string() + ".bak"),
        std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
      XELOGW("PatchUpdate: {}: backup failed ({})", remote.name, ec.message());
      std::lock_guard<std::mutex> lock(progress->mutex);
      ++progress->failed;
      return;
    }
  }
  const auto restore_backup = [&]() {
    if (!local_exists) {
      return;
    }
    std::error_code restore_ec;
    std::filesystem::copy_file(
        std::filesystem::path(path.string() + ".bak"), path,
        std::filesystem::copy_options::overwrite_existing, restore_ec);
  };
  if (!WriteAllBytes(path, bytes)) {
    XELOGW("PatchUpdate: {}: write failed", remote.name);
    restore_backup();
    std::lock_guard<std::mutex> lock(progress->mutex);
    ++progress->failed;
    return;
  }
  size_t preserved = 0;
  if (local_exists && local.title_id != static_cast<uint32_t>(-1)) {
    bool ok = false;
    std::tie(ok, preserved) = MergeLocalEnabledFlags(local, path);
    if (!ok) {
      XELOGW("PatchUpdate: {}: toggle merge failed, restored .bak",
             remote.name);
      restore_backup();
      std::lock_guard<std::mutex> lock(progress->mutex);
      ++progress->failed;
      return;
    }
  }
  std::lock_guard<std::mutex> lock(progress->mutex);
  ++progress->updated;
  progress->preserved += preserved;
}

}  // namespace

void UpdateGamePatchesAsync(const std::filesystem::path& patches_dir,
                            std::shared_ptr<PatchUpdateProgress> progress) {
  std::thread([patches_dir, progress]() {
    const auto started = std::chrono::steady_clock::now();
    XELOGI("PatchUpdate: fetching patch list for {}",
           xe::path_to_utf8(patches_dir));
    std::vector<RemoteFile> files;
    if (!ListRemotePatches(&files)) {
      std::lock_guard<std::mutex> lock(progress->mutex);
      progress->error =
          "Could not fetch the patch list. Check the network connection "
          "and try again.";
      progress->finished = true;
      return;
    }
    {
      std::lock_guard<std::mutex> lock(progress->mutex);
      progress->total = files.size();
    }
    XELOGI("PatchUpdate: {} files listed", files.size());
    std::error_code ec;
    std::filesystem::create_directories(patches_dir, ec);
    // Fixed worker pool over a shared file queue: 8 parallel transfers,
    // each with its own connection-warm libcurl handle. All shared state
    // stays behind progress->mutex; files never overlap.
    const size_t worker_count =
        std::max<size_t>(1, std::min<size_t>(8, files.size()));
    std::atomic<size_t> next{0};
    std::vector<std::thread> pool;
    pool.reserve(worker_count);
    for (size_t w = 0; w < worker_count; ++w) {
      pool.emplace_back([&]() {
        for (;;) {
          const size_t i = next.fetch_add(1);
          if (i >= files.size() || progress->cancelled.load()) {
            break;
          }
          {
            std::lock_guard<std::mutex> lock(progress->mutex);
            progress->current = files[i].name;
          }
          std::string bytes;
          const std::string url = EncodeUrl(files[i].download_url);
          if (!HttpGet(url, &bytes)) {
            XELOGW("PatchUpdate: {}: download failed ({})", files[i].name, url);
            std::lock_guard<std::mutex> lock(progress->mutex);
            ++progress->failed;
            ++progress->done;
            continue;
          }
          UpdateOneFile(patches_dir, files[i], bytes, progress);
          std::lock_guard<std::mutex> lock(progress->mutex);
          ++progress->done;
        }
      });
    }
    for (auto& worker : pool) {
      worker.join();
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - started);
    size_t updated, unchanged, failed, preserved;
    bool cancelled;
    {
      std::lock_guard<std::mutex> lock(progress->mutex);
      progress->finished = true;
      updated = progress->updated;
      unchanged = progress->unchanged;
      failed = progress->failed;
      preserved = progress->preserved;
      cancelled = progress->cancelled.load();
    }
    XELOGI(
        "PatchUpdate: finished in {}s: {} updated, {} unchanged, {} "
        "failed, {} toggles kept{}",
        elapsed.count(), updated, unchanged, failed, preserved,
        cancelled ? " (cancelled)" : "");
  }).detach();
}

class WxPatchUpdateDialog : public wxDialog {
 public:
  WxPatchUpdateDialog(wxWindow* parent,
                      const std::filesystem::path& patches_dir)
      // NOTE: size through parent, not this: FromDIP on an unconstructed
      // window dereferences unset internals and AVs (GetDPIHelper).
      : wxDialog(parent, wxID_ANY, "Update Game Patches", wxDefaultPosition,
                 parent->FromDIP(wxSize(420, 140)), wxDEFAULT_DIALOG_STYLE),
        state_(std::make_shared<PatchUpdateProgress>()) {
    auto* outer = new wxBoxSizer(wxVERTICAL);
    status_ = new wxStaticText(this, wxID_ANY, "Fetching patch list...");
    outer->Add(status_, 0, wxEXPAND | wxALL, FromDIP(8));
    gauge_ = new wxGauge(this, wxID_ANY, 100);
    outer->Add(gauge_, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(8));
    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    buttons->AddStretchSpacer(1);
    cancel_ = new wxButton(this, wxID_CANCEL, "Cancel");
    buttons->Add(cancel_, 0, wxRIGHT | wxBOTTOM, FromDIP(8));
    outer->Add(buttons, 0, wxEXPAND | wxTOP, FromDIP(8));
    SetSizer(outer);

    cancel_->Bind(wxEVT_BUTTON, &WxPatchUpdateDialog::OnCancel, this);
    Bind(wxEVT_CLOSE_WINDOW, &WxPatchUpdateDialog::OnCloseWindow, this);
    poll_timer_.SetOwner(this);
    Bind(wxEVT_TIMER, &WxPatchUpdateDialog::OnPoll, this);
    UpdateGamePatchesAsync(patches_dir, state_);
    poll_timer_.Start(100);
  }

  std::shared_ptr<PatchUpdateProgress> state() const { return state_; }

 private:
  void OnCloseWindow(wxCloseEvent& event) {
    // Same as Cancel, but hold the dialog open until the worker notices:
    // closing early would show the summary while files are still landing.
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (!state_->finished) {
      state_->cancelled.store(true);
      cancel_->Disable();
      status_->SetLabel("Cancelling after the current file...");
      event.Veto();
      return;
    }
    event.Skip();
  }
  void OnCancel(wxCommandEvent&) {
    state_->cancelled.store(true);
    cancel_->Disable();
    status_->SetLabel("Cancelling after the current file...");
  }

  void OnPoll(wxTimerEvent&) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->total > 0) {
      gauge_->SetRange(int(state_->total));
      gauge_->SetValue(int(std::min(state_->done, state_->total)));
      status_->SetLabel(WxLabel("Updating " + state_->current + " (" +
                                std::to_string(state_->done) + "/" +
                                std::to_string(state_->total) + ")"));
    } else {
      gauge_->Pulse();
    }
    if (state_->finished) {
      poll_timer_.Stop();
      EndModal(state_->cancelled.load() ? wxID_CANCEL : wxID_OK);
    }
  }

  std::shared_ptr<PatchUpdateProgress> state_;
  wxStaticText* status_ = nullptr;
  wxGauge* gauge_ = nullptr;
  wxButton* cancel_ = nullptr;
  wxTimer poll_timer_;
};

void ShowPatchUpdateDialog(wxWindow* parent,
                           const std::filesystem::path& patches_dir) {
  if (!parent) {
    return;
  }
  WxPatchUpdateDialog dialog(parent, patches_dir);
  dialog.ShowModal();
  const auto state = dialog.state();
  std::lock_guard<std::mutex> lock(state->mutex);
  if (!state->error.empty() && state->updated == 0 && state->done == 0) {
    wxMessageBox(WxLabel(state->error), "Update Game Patches",
                 wxOK | wxICON_ERROR, parent);
    return;
  }
  std::string summary =
      "Updated: " + std::to_string(state->updated) +
      "\nUnchanged: " + std::to_string(state->unchanged) +
      "\nFailed: " + std::to_string(state->failed) +
      "\nKept patch toggles: " + std::to_string(state->preserved);
  if (state->cancelled.load()) {
    summary += "\n\nCancelled - remaining files were skipped.";
  }
  if (!state->error.empty()) {
    summary += "\n\n" + state->error;
  }
  summary += "\n\nChanges apply the next time a title is launched.";
  wxMessageBox(WxLabel(summary), "Update Game Patches",
               (state->failed || !state->error.empty() ? wxICON_WARNING
                                                       : wxICON_INFORMATION) |
                   wxOK,
               parent);
}

}  // namespace wx_ui
}  // namespace app
}  // namespace xe
