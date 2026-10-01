/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// Picker for Scan Folder results: lets the user choose which discovered
// discs get added to the library. Unreadable entries never reach the dialog
// (CollectNewGamesFromFolder omits them); everything shown starts checked.

#include "xenia/app/wx/wx_game_scan_dialog.h"

#include "xenia/app/wx/wx_window.h"
#include "xenia/app/wx/wx_window_priv.h"

#include <atomic>
#include <cstring>
#include <mutex>
#include <set>
#include <thread>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/dcmemory.h>
#include <wx/dialog.h>
#include <wx/dirdlg.h>
#include <wx/display.h>
#include <wx/filedlg.h>
#include <wx/font.h>
#include <wx/image.h>
#include <wx/mstream.h>
#include <wx/progdlg.h>
#include <wx/scrolwin.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>

#include "xenia/app/wx/wx_util.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"

namespace xe {
namespace app {
namespace wx_ui {

namespace {

wxBitmap PlaceholderBitmap(int px) {
  wxBitmap bmp(px, px, 32);
  wxMemoryDC dc(bmp);
  dc.SetBackground(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE)));
  dc.Clear();
  dc.SetTextForeground(
      wxColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT)));
  dc.SetFont(wxFont(px / 3, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL,
                    wxFONTWEIGHT_BOLD));
  dc.DrawLabel("?", wxRect(0, 0, px, px), wxALIGN_CENTER);
  dc.SelectObject(wxNullBitmap);
  return bmp;
}

wxBitmap IconForBytes(const std::vector<uint8_t>& bytes, int px) {
  if (!bytes.empty()) {
    wxMemoryInputStream stream(bytes.data(), bytes.size());
    wxImage image;
    if (image.LoadFile(stream, wxBITMAP_TYPE_ANY) && image.IsOk()) {
      return wxBitmap(image.Scale(px, px, wxIMAGE_QUALITY_HIGH));
    }
  }
  return PlaceholderBitmap(px);
}

std::string GameFileTypeName(GameFileType type) {
  switch (type) {
    case GameFileType::kXex:
      return "XEX";
    case GameFileType::kIso:
      return "ISO";
    case GameFileType::kZar:
      return "ZAR";
    case GameFileType::kStfs:
      return "STFS";
    case GameFileType::kSvod:
      return "SVOD";
    case GameFileType::kUnknown:
    default:
      return "Unknown";
  }
}

// Modal return codes for the source choice (Cancel uses wxID_CANCEL).
constexpr int kSourceFiles = wxID_HIGHEST + 2;
constexpr int kSourceFolder = wxID_HIGHEST + 3;

}  // namespace

AddSource AskAddSource(wxWindow* parent, const std::string& title,
                       const std::string& prompt) {
  if (!parent) {
    return AddSource::kCancel;
  }
  wxDialog dialog(parent, wxID_ANY, WxLabel(title), wxDefaultPosition,
                  wxDefaultSize, wxDEFAULT_DIALOG_STYLE);
  auto* outer = new wxBoxSizer(wxVERTICAL);
  outer->Add(new wxStaticText(&dialog, wxID_ANY, WxLabel(prompt)), 0, wxALL,
             dialog.FromDIP(10));
  auto* buttons = new wxBoxSizer(wxHORIZONTAL);
  auto* files = new wxButton(&dialog, kSourceFiles, "Select Files...");
  // Stock IDs close the modal loop on their own; custom codes need an
  // explicit EndModal.
  files->Bind(
      wxEVT_BUTTON,
      [&dialog](wxCommandEvent&) { dialog.EndModal(kSourceFiles); },
      kSourceFiles);
  buttons->Add(files, 0, wxRIGHT, dialog.FromDIP(8));
  auto* folder = new wxButton(&dialog, kSourceFolder, "Scan Folder...");
  folder->Bind(
      wxEVT_BUTTON,
      [&dialog](wxCommandEvent&) { dialog.EndModal(kSourceFolder); },
      kSourceFolder);
  buttons->Add(folder, 0, wxRIGHT, dialog.FromDIP(8));
  buttons->Add(new wxButton(&dialog, wxID_CANCEL), 0);
  outer->Add(buttons, 0, wxEXPAND | wxALL, dialog.FromDIP(10));
  dialog.SetSizerAndFit(outer);
  const int code = dialog.ShowModal();
  if (code == kSourceFiles) {
    return AddSource::kFiles;
  }
  if (code == kSourceFolder) {
    return AddSource::kFolder;
  }
  return AddSource::kCancel;
}

AddSource AskAddSource(WxWindow* window, const std::string& title,
                       const std::string& prompt) {
  if (!window) {
    return AddSource::kCancel;
  }
  return AskAddSource(window->view() ? static_cast<wxWindow*>(window->view())
                                     : static_cast<wxWindow*>(window->frame()),
                      title, prompt);
}

namespace {

std::vector<ScannedGameItem> CollectNewGames(
    wxWindow* parent, const std::filesystem::path& dir,
    const std::vector<std::filesystem::path>& paths,
    const std::vector<std::filesystem::path>& known_paths) {
  // Folder discovery (skipped for explicit files), membership filtering, and
  // metadata reads all run on the worker: the dialog below exists from the
  // start, so its Pulse/Update pump keeps the UI alive (and cancellable)
  // instead of freezing on the folder walk. The fixed maximum normalizes
  // the determinate phase, whose total is only known after discovery.
  constexpr int kProgressMax = 1000;
  struct ScanJob {
    // 0 = discovering/filtering (indeterminate), 1 = reading metadata.
    std::atomic<int> phase{0};
    std::atomic<size_t> done{0};
    std::atomic<size_t> total{0};
    std::atomic<bool> cancel{false};
    std::atomic<bool> finished{false};
    std::mutex current_mutex;
    std::string current;
    std::vector<ScannedGameItem> items;
  };
  auto job = std::make_shared<ScanJob>();
  std::thread worker([dir, paths, known_paths, job]() {
    // Explicit files skip discovery; folders are walked first.
    std::vector<std::filesystem::path> discovered =
        dir.empty() ? paths : DiscoverGameFiles(dir);
    if (job->cancel.load()) {
      job->finished.store(true);
      return;
    }
    // Only offer games not already in the library (same membership check
    // ScanInstalledGames uses).
    std::vector<std::filesystem::path> fresh;
    for (const auto& path : discovered) {
      if (job->cancel.load()) {
        break;
      }
      bool known = false;
      for (const auto& known_path : known_paths) {
        std::error_code ec = {};
        if (std::filesystem::equivalent(known_path, path, ec)) {
          known = true;
          break;
        }
      }
      if (!known) {
        fresh.push_back(path);
      }
    }
    job->total.store(fresh.size());
    job->phase.store(1);
    for (size_t i = 0; i < fresh.size(); i++) {
      if (job->cancel.load()) {
        break;
      }
      const auto& path = fresh[i];
      {
        std::lock_guard<std::mutex> lock(job->current_mutex);
        job->current = xe::path_to_utf8(path.filename());
      }
      GameMeta meta;
      if (!ReadGameMeta(path, meta) || !meta.ok || meta.title_id.empty() ||
          meta.title_id == "00000000") {
        XELOGW("Library: skipping unrecognized file {}",
               xe::path_to_utf8(path));
        job->done.store(i + 1);
        continue;
      }
      if (meta.name.empty()) {
        meta.name = xe::path_to_utf8(path.stem());
      }
      ScannedGameItem item;
      item.path = path;
      item.meta = std::move(meta);
      job->items.push_back(std::move(item));
      job->done.store(i + 1);
    }
    job->finished.store(true);
  });
  const std::string discover_msg =
      dir.empty() ? "Reading game files..."
                  : "Discovering files in " + xe::path_to_utf8(dir.filename());
  wxProgressDialog progress("Scanning games", WxLabel(discover_msg),
                            kProgressMax, parent,
                            wxPD_APP_MODAL | wxPD_CAN_ABORT | wxPD_AUTO_HIDE);
  while (!job->finished.load()) {
    if (job->phase.load() == 0) {
      if (!progress.Pulse(WxLabel(discover_msg))) {
        job->cancel.store(true);
      }
    } else {
      const size_t total = job->total.load();
      const size_t done = job->done.load();
      std::string current;
      {
        std::lock_guard<std::mutex> lock(job->current_mutex);
        current = job->current;
      }
      const int value = total ? int(done * kProgressMax / total) : kProgressMax;
      if (!progress.Update(value, WxLabel(current))) {
        job->cancel.store(true);
      }
    }
    wxMilliSleep(50);
  }
  worker.join();
  progress.Update(kProgressMax);
  if (job->cancel.load()) {
    return {};
  }
  return std::move(job->items);
}

}  // namespace

std::vector<ScannedGameItem> CollectNewGamesFromFolder(
    wxWindow* parent, const std::filesystem::path& dir,
    const std::vector<std::filesystem::path>& known_paths) {
  if (!parent) {
    return {};
  }
  return CollectNewGames(parent, dir, {}, known_paths);
}

std::vector<ScannedGameItem> CollectNewGamesFromFiles(
    wxWindow* parent, const std::vector<std::filesystem::path>& paths,
    const std::vector<std::filesystem::path>& known_paths) {
  if (!parent || paths.empty()) {
    return {};
  }
  return CollectNewGames(parent, {}, paths, known_paths);
}

class WxScanPickerDialog : public wxDialog {
 public:
  WxScanPickerDialog(wxWindow* parent,
                     const std::vector<ScannedGameItem>& items,
                     const CompatMap& compat,
                     const std::vector<std::filesystem::path>& known_paths)
      : wxDialog(parent, wxID_ANY, "Select Games to Add", wxDefaultPosition,
                 wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
        compat_(compat),
        known_paths_(known_paths),
        icon_px_(FromDIP(64)) {
    // Image handlers are already initialized by WxLibraryView.

    auto* outer = new wxBoxSizer(wxVERTICAL);
    auto* header = new wxBoxSizer(wxHORIZONTAL);
    header->Add(new wxStaticText(this, wxID_ANY,
                                 WxLabel("Select the games to add to the "
                                         "library:")),
                0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    header->AddStretchSpacer(1);
    auto* add_more = new wxButton(this, wxID_ANY, "Add More...");
    add_more->Bind(wxEVT_BUTTON, &WxScanPickerDialog::OnAddMore, this);
    header->Add(add_more, 0);
    outer->Add(header, 0, wxEXPAND | wxALL, FromDIP(10));

    scrolled_ = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition,
                                     wxDefaultSize, wxVSCROLL);
    list_ = new wxBoxSizer(wxVERTICAL);
    for (const auto& item : items) {
      AddItemRow(item);
    }
    scrolled_->SetSizer(list_);
    scrolled_->SetScrollRate(0, FromDIP(10));
    scrolled_->SetMinSize(FromDIP(wxSize(620, 360)));
    outer->Add(scrolled_, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(10));

    auto* bottom = new wxBoxSizer(wxHORIZONTAL);
    auto* select_all = new wxButton(this, wxID_ANY, "Select All");
    select_all->Bind(wxEVT_BUTTON, &WxScanPickerDialog::OnSelectAll, this);
    bottom->Add(select_all, 0, wxRIGHT, FromDIP(8));
    auto* select_none = new wxButton(this, wxID_ANY, "Select None");
    select_none->Bind(wxEVT_BUTTON, &WxScanPickerDialog::OnSelectNone, this);
    bottom->Add(select_none, 0);
    bottom->AddStretchSpacer(1);
    auto* ok = new wxButton(this, wxID_OK, "Add Selected");
    ok->SetDefault();
    bottom->Add(ok, 0, wxRIGHT, FromDIP(8));
    bottom->Add(new wxButton(this, wxID_CANCEL), 0);
    outer->Add(bottom, 0, wxEXPAND | wxALL, FromDIP(10));

    SetSizer(outer);
    Fit();
    wxSize size = GetSize();
    size.IncTo(FromDIP(wxSize(660, 480)));
    wxDisplay display(wxDisplay::GetFromWindow(this));
    if (display.IsOk()) {
      const wxRect work = display.GetClientArea();
      size.x = std::min(size.x, work.width);
      size.y = std::min(size.y, work.height);
    }
    SetSize(size);
    Layout();
  }

  std::vector<std::filesystem::path> SelectedPaths() const {
    std::vector<std::filesystem::path> selected;
    for (size_t i = 0; i < checks_.size() && i < paths_.size(); i++) {
      if (checks_[i]->GetValue()) {
        selected.push_back(paths_[i]);
      }
    }
    return selected;
  }

 private:
  void AddItemRow(const ScannedGameItem& item) {
    // Exact-path dedup: re-scanning an overlapping folder must not list a
    // game twice (library discs never reach here — the collector filters
    // those via known_paths_).
    if (!shown_.insert(item.path).second) {
      return;
    }
    paths_.push_back(item.path);
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    auto* check = new wxCheckBox(scrolled_, wxID_ANY, wxString());
    check->SetValue(true);
    checks_.push_back(check);
    row->Add(check, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    row->Add(new wxStaticBitmap(scrolled_, wxID_ANY,
                                IconForBytes(item.meta.icon_bytes, icon_px_)),
             0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    auto* texts = new wxBoxSizer(wxVERTICAL);
    // Row 1: title + compatibility badge.
    auto* title_row = new wxBoxSizer(wxHORIZONTAL);
    auto* title =
        new wxStaticText(scrolled_, wxID_ANY, WxLabel(item.meta.name));
    wxFont bold = title->GetFont();
    bold.SetWeight(wxFONTWEIGHT_BOLD);
    title->SetFont(bold);
    title_row->Add(title, 0, wxALIGN_CENTER_VERTICAL);
    // Always show a rating, Unknown included. A missing database
    // (no compatibility_data.json yet, or a failed download) leaves the map
    // empty, and a title with no report is absent from it; rendering nothing
    // for either read as "no data" rather than "not rated", and disagreed with
    // the library view, which shows the Unknown badge for the same entries.
    CompatRating rating = CompatRating::kUnknown;
    if (const CompatInfo* info = FindCompat(compat_, item.meta.title_id)) {
      rating = info->rating;
    }
    auto* rating_text =
        new wxStaticText(scrolled_, wxID_ANY, CompatName(rating));
    wxFont rating_font = rating_text->GetFont();
    rating_font.SetWeight(wxFONTWEIGHT_BOLD);
    rating_text->SetFont(rating_font);
    rating_text->SetForegroundColour(CompatColor(rating));
    title_row->Add(rating_text, 0, wxLEFT | wxALIGN_CENTER_VERTICAL,
                   FromDIP(8));
    texts->Add(title_row, 0, wxEXPAND | wxBOTTOM, FromDIP(2));
    // Row 2: labeled IDs (version omitted when unknown).
    std::string ids_line = "Title ID: " + item.meta.title_id +
                           "  |  Media ID: " + item.meta.media_id;
    if (!item.meta.version.empty()) {
      ids_line += "  |  Version: " + item.meta.version;
    }
    texts->Add(new wxStaticText(scrolled_, wxID_ANY, WxLabel(ids_line)), 0,
               wxEXPAND | wxBOTTOM, FromDIP(2));
    // Row 3: format tag + full path.
    const std::string location = xe::path_to_utf8(item.path);
    auto* path_text = new wxStaticText(
        scrolled_, wxID_ANY,
        WxLabel("[" + GameFileTypeName(item.meta.type) + "] " + location));
    path_text->SetToolTip(WxLabel(location));
    texts->Add(path_text, 0, wxEXPAND);
    row->Add(texts, 1, wxALIGN_CENTER_VERTICAL);
    list_->Add(row, 0, wxEXPAND | wxALL, FromDIP(4));
  }

  void OnSelectAll(wxCommandEvent&) { SetAll(true); }
  void OnSelectNone(wxCommandEvent&) { SetAll(false); }

  void OnAddMore(wxCommandEvent&) {
    switch (
        AskAddSource(this, "Add Games",
                     "Add more games from files or by scanning a folder?")) {
      case AddSource::kFiles: {
        wxFileDialog file_dialog(
            this, "Add Game", wxString(), wxString(),
            "Xbox 360 games "
            "(*.xex;*.iso;*.xiso;*.zar)|*.xex;*.iso;*.xiso;*.zar|"
            "All files (*.*)|*.*",
            wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_MULTIPLE);
        if (file_dialog.ShowModal() != wxID_OK) {
          return;
        }
        wxArrayString wx_paths;
        file_dialog.GetPaths(wx_paths);
        std::vector<std::filesystem::path> paths;
        for (const auto& p : wx_paths) {
          paths.push_back(WxToPath(p));
        }
        AppendMore(CollectNewGamesFromFiles(this, paths, known_paths_));
        break;
      }
      case AddSource::kFolder: {
        wxDirDialog dir_dialog(this, "Scan Folder for Games");
        if (dir_dialog.ShowModal() != wxID_OK) {
          return;
        }
        AppendMore(CollectNewGamesFromFolder(
            this, WxToPath(dir_dialog.GetPath()), known_paths_));
        break;
      }
      case AddSource::kCancel:
        break;
    }
  }

  void AppendMore(std::vector<ScannedGameItem> more) {
    for (const auto& item : more) {
      AddItemRow(item);
    }
    scrolled_->FitInside();
    scrolled_->Layout();
    Layout();
  }

  void SetAll(bool value) {
    for (auto* check : checks_) {
      check->SetValue(value);
    }
  }

  const CompatMap& compat_;
  std::vector<std::filesystem::path> known_paths_;
  int icon_px_ = 0;
  wxScrolledWindow* scrolled_ = nullptr;
  wxBoxSizer* list_ = nullptr;
  std::vector<wxCheckBox*> checks_;
  std::vector<std::filesystem::path> paths_;
  std::set<std::filesystem::path> shown_;
};

std::vector<std::filesystem::path> ShowScanPickerDialog(
    wxWindow* parent, const std::vector<ScannedGameItem>& items,
    const CompatMap& compat,
    const std::vector<std::filesystem::path>& known_paths) {
  if (!parent || items.empty()) {
    return {};
  }
  WxScanPickerDialog dialog(parent, items, compat, known_paths);
  if (dialog.ShowModal() != wxID_OK) {
    return {};
  }
  return dialog.SelectedPaths();
}

}  // namespace wx_ui
}  // namespace app
}  // namespace xe
