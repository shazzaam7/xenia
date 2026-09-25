/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// Per-title patch editor: one treebook page per installed patch file, each
// with a checkbox per [[patch]] entry, plus a search box filtering entries
// across files. Persisted by flipping is_enabled in place: the rewrite is
// surgical (only the is_enabled lines change, everything else is
// byte-preserved) and verified by re-parsing; the engine honors the flag
// natively at apply time, so no engine change is needed and the files stay
// hand-editable.

#include "xenia/app/wx/wx_patch_dialog.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/dialog.h>
#include <wx/msgdlg.h>
#include <wx/scrolwin.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/srchctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/timer.h>
#include <wx/treebook.h>

#include "xenia/app/wx/wx_util.h"
#include "xenia/base/filesystem.h"
#include "xenia/patcher/patch_db.h"

namespace xe {
namespace app {
namespace wx_ui {
namespace {

constexpr int kNamePx = 280;

std::string TrimCopy(std::string_view text) {
  const auto first = text.find_first_not_of(" \t\r");
  if (first == std::string_view::npos) {
    return "";
  }
  return std::string(
      text.substr(first, text.find_last_not_of(" \t\r") - first + 1));
}

std::string ToLowerCopy(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return char(std::tolower(c)); });
  return text;
}

bool EqualsInsensitive(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

bool IsHexId(std::string_view text) {
  if (text.size() != 8) {
    return false;
  }
  for (char c : text) {
    const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                     (c >= 'A' && c <= 'F');
    if (!hex) {
      return false;
    }
  }
  return true;
}

// Display label for a patch file: the descriptive part between the
// "<TITLEID> - " prefix and the ".patch.toml" suffix, or the stem as-is.
std::string FileLabel(const std::string& filename) {
  constexpr std::string_view kSuffix = ".patch.toml";
  std::string_view stem = filename;
  if (stem.size() > kSuffix.size() &&
      stem.substr(stem.size() - kSuffix.size()) == kSuffix) {
    stem.remove_suffix(kSuffix.size());
  }
  if (stem.size() > 11 && IsHexId(stem.substr(0, 8)) &&
      stem.substr(8, 3) == " - ") {
    stem.remove_prefix(11);
  }
  return std::string(stem);
}

// Case-insensitive substring match over an entry's name, description, author
// and file name. The query is stored already lowercased.
bool MatchesPatchSearch(const patcher::PatchInfoEntry& entry,
                        const std::string& filename, const std::string& query) {
  std::string haystack = entry.patch_name + "\n" + entry.patch_desc + "\n" +
                         entry.patch_author + "\n" + filename;
  return ToLowerCopy(haystack).find(query) != std::string::npos;
}

struct PatchFile {
  std::filesystem::path path;
  std::string filename;
  patcher::PatchFileEntry parsed;
};

struct PatchRow {
  size_t file = 0;
  size_t entry = 0;
  wxCheckBox* check = nullptr;
};

class WxPatchDialog : public wxDialog {
 public:
  WxPatchDialog(wxWindow* parent, const std::filesystem::path& patches_dir,
                const std::string& title_id, const std::string& title_name)
      : wxDialog(parent, wxID_ANY,
                 WxLabel("Patches - " + title_name + " (" + title_id + ")"),
                 wxDefaultPosition, wxDefaultSize,
                 wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
        patches_dir_(xe::path_to_utf8(patches_dir)) {
    if (IsHexId(title_id)) {
      CollectFiles(patches_dir, title_id);
    }

    search_timer_.SetOwner(this);

    auto* outer = new wxBoxSizer(wxVERTICAL);
    search_ = new wxSearchCtrl(this, wxID_ANY);
    search_->SetHint("Search patches");
    search_->SetDescriptiveText("Search patches by name or description");
    search_->ShowCancelButton(true);
    search_->SetToolTip("Filter patches by name, description or author.");
    outer->Add(search_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));

    book_ = new wxTreebook(this, wxID_ANY);
    outer->Add(book_, 1, wxEXPAND | wxALL, FromDIP(8));
    BuildPages(nullptr);

    auto* footnote = new wxStaticText(
        this, wxID_ANY,
        "Patch changes apply the next time the title is launched.");
    footnote->Enable(false);
    outer->Add(footnote, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));

    save_button_ = new wxButton(this, wxID_SAVE, "Save");
    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    buttons->AddStretchSpacer(1);
    buttons->Add(save_button_, 0, wxRIGHT | wxBOTTOM, FromDIP(8));
    outer->Add(buttons, 0, wxEXPAND | wxBOTTOM, FromDIP(8));
    SetSizer(outer);
    Fit();
    wxSize size = GetSize();
    size.x = std::min(size.x, FromDIP(680));
    size.y = std::min(size.y, FromDIP(640));
    SetSize(size);

    save_button_->Bind(wxEVT_BUTTON, &WxPatchDialog::OnSave, this);
    search_->Bind(wxEVT_TEXT, &WxPatchDialog::OnSearch, this);
    Bind(wxEVT_TIMER, &WxPatchDialog::OnSearchTimer, this,
         search_timer_.GetId());
  }

 private:
  void CollectFiles(const std::filesystem::path& patches_dir,
                    const std::string& title_id) {
    std::error_code ec;
    if (!std::filesystem::is_directory(patches_dir, ec)) {
      return;
    }
    for (const auto& dir_entry :
         std::filesystem::directory_iterator(patches_dir, ec)) {
      if (!dir_entry.is_regular_file(ec)) {
        continue;
      }
      const std::string filename =
          xe::path_to_utf8(dir_entry.path().filename());
      constexpr std::string_view kSuffix = ".patch.toml";
      if (filename.size() <= kSuffix.size() ||
          filename.substr(filename.size() - kSuffix.size()) != kSuffix) {
        continue;
      }
      if (!EqualsInsensitive(filename.substr(0, 8), title_id)) {
        continue;
      }
      PatchFile file;
      file.path = dir_entry.path();
      file.filename = filename;
      // Static parse: independent of the apply_patches master toggle, so the
      // editor works even when patching is globally disabled.
      file.parsed = patcher::PatchDB::ReadPatchFile(file.path);
      files_.push_back(std::move(file));
    }
    std::ranges::sort(files_, [](const PatchFile& a, const PatchFile& b) {
      return a.filename < b.filename;
    });
  }

  // Sizes a book page to its content with a scroll fallback.
  static void FinishPage(wxScrolledWindow* scrolled, wxSizer* col) {
    scrolled->SetSizer(col);
    const wxSize content = col->CalcMin();
    scrolled->SetMinSize(
        wxSize(std::min(content.GetWidth() +
                            wxSystemSettings::GetMetric(wxSYS_VSCROLL_X) +
                            scrolled->FromDIP(16),
                        scrolled->FromDIP(560)),
               std::min(content.GetHeight() + scrolled->FromDIP(16),
                        scrolled->FromDIP(470))));
    auto* page = scrolled->GetParent();
    auto* page_sizer = new wxBoxSizer(wxVERTICAL);
    page_sizer->Add(scrolled, 1, wxEXPAND | wxALL, scrolled->FromDIP(4));
    page->SetSizer(page_sizer);
    page->Layout();
    scrolled->Layout();
    scrolled->FitInside();
  }

  // Full file name header, so the tab label can stay short.
  static void AddFileHeader(wxScrolledWindow* scrolled, wxBoxSizer* col,
                            const std::string& filename) {
    auto* header = new wxStaticText(scrolled, wxID_ANY, WxLabel(filename));
    header->SetForegroundColour(
        wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    col->Add(header, 0, wxEXPAND | wxALL, scrolled->FromDIP(4));
  }

  void AddPatchRow(wxWindow* parent, wxBoxSizer* col, size_t f, size_t e,
                   const std::map<std::pair<size_t, size_t>, bool>* seeds) {
    const auto& entry = files_[f].parsed.patch_info[e];
    const std::string label = entry.patch_name.empty()
                                  ? ("Patch " + std::to_string(e + 1))
                                  : entry.patch_name;
    auto* row_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto* check = new wxCheckBox(parent, wxID_ANY, wxEmptyString);
    bool enabled = entry.is_enabled;
    if (seeds) {
      const auto it = seeds->find({f, e});
      if (it != seeds->end()) {
        enabled = it->second;
      }
    }
    check->SetValue(enabled);
    row_sizer->Add(check, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
    auto* texts = new wxBoxSizer(wxVERTICAL);
    auto* name = new wxStaticText(parent, wxID_ANY, WxLabel(label));
    name->SetMinSize(wxSize(FromDIP(kNamePx), -1));
    texts->Add(name, 0, wxEXPAND);
    if (!entry.patch_author.empty()) {
      auto* author =
          new wxStaticText(parent, wxID_ANY, WxLabel(entry.patch_author));
      author->SetForegroundColour(
          wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
      texts->Add(author, 0, wxEXPAND);
    }
    row_sizer->Add(texts, 0, wxALIGN_CENTER_VERTICAL);
    if (!entry.patch_desc.empty()) {
      const wxString tip = WxLabel(entry.patch_desc);
      check->SetToolTip(tip);
      name->SetToolTip(tip);
    }
    col->Add(row_sizer, 0, wxEXPAND | wxALL, FromDIP(2));
    PatchRow row;
    row.file = f;
    row.entry = e;
    row.check = check;
    rows_.push_back(row);
  }

  // (Re)creates the book contents. seeds carries unsaved checkbox states
  // across rebuilds (search typing), keyed by file/entry index.
  void BuildPages(const std::map<std::pair<size_t, size_t>, bool>* seeds) {
    book_->DeleteAllPages();
    rows_.clear();

    if (!search_query_.empty()) {
      BuildSearchPage(seeds);
      return;
    }
    if (files_.empty()) {
      auto* page = new wxPanel(book_, wxID_ANY);
      auto* scrolled = new wxScrolledWindow(page, wxID_ANY, wxDefaultPosition,
                                            wxDefaultSize, wxVSCROLL);
      scrolled->SetScrollRate(0, FromDIP(10));
      auto* col = new wxBoxSizer(wxVERTICAL);
      col->Add(new wxStaticText(scrolled, wxID_ANY,
                                WxLabel("No patch files installed for this "
                                        "title.\n\nPlace *.patch.toml files in "
                                        "the patches folder:\n" +
                                        patches_dir_)),
               0, wxALL, FromDIP(8));
      FinishPage(scrolled, col);
      book_->AddPage(page, "No patches", true);
      return;
    }
    for (size_t f = 0; f < files_.size(); ++f) {
      const PatchFile& file = files_[f];
      auto* page = new wxPanel(book_, wxID_ANY);
      auto* scrolled = new wxScrolledWindow(page, wxID_ANY, wxDefaultPosition,
                                            wxDefaultSize, wxVSCROLL);
      scrolled->SetScrollRate(0, FromDIP(10));
      auto* col = new wxBoxSizer(wxVERTICAL);
      AddFileHeader(scrolled, col, file.filename);
      if (file.parsed.title_id == static_cast<uint32_t>(-1)) {
        auto* broken = new wxStaticText(
            scrolled, wxID_ANY, "Could not parse this file - left untouched.");
        broken->Enable(false);
        col->Add(broken, 0, wxALL, FromDIP(4));
      } else if (file.parsed.patch_info.empty()) {
        auto* empty = new wxStaticText(scrolled, wxID_ANY,
                                       "This file contains no patches.");
        empty->Enable(false);
        col->Add(empty, 0, wxALL, FromDIP(4));
      }
      for (size_t e = 0; e < file.parsed.patch_info.size(); ++e) {
        AddPatchRow(scrolled, col, f, e, seeds);
      }
      FinishPage(scrolled, col);
      book_->AddPage(page, WxLabel(FileLabel(file.filename)), f == 0);
    }
  }

  // Single flat page with every entry matching the search query, grouped by
  // file and regardless of which page showed them.
  void BuildSearchPage(const std::map<std::pair<size_t, size_t>, bool>* seeds) {
    auto* page = new wxPanel(book_, wxID_ANY);
    auto* scrolled = new wxScrolledWindow(page, wxID_ANY, wxDefaultPosition,
                                          wxDefaultSize, wxVSCROLL);
    scrolled->SetScrollRate(0, FromDIP(10));
    auto* col = new wxBoxSizer(wxVERTICAL);
    size_t matches = 0;
    for (size_t f = 0; f < files_.size(); ++f) {
      const PatchFile& file = files_[f];
      if (file.parsed.title_id == static_cast<uint32_t>(-1)) {
        continue;
      }
      bool header_added = false;
      for (size_t e = 0; e < file.parsed.patch_info.size(); ++e) {
        if (!MatchesPatchSearch(file.parsed.patch_info[e], file.filename,
                                search_query_)) {
          continue;
        }
        if (!header_added) {
          AddFileHeader(scrolled, col, file.filename);
          header_added = true;
        }
        ++matches;
        AddPatchRow(scrolled, col, f, e, seeds);
      }
    }
    if (!matches) {
      col->Add(
          new wxStaticText(scrolled, wxID_ANY, "No patches match this search."),
          0, wxALL, FromDIP(8));
    }
    FinishPage(scrolled, col);
    book_->AddPage(page,
                   WxLabel("Search results (" + std::to_string(matches) + ")"),
                   true);
  }

  std::map<std::pair<size_t, size_t>, bool> CollectValues() const {
    std::map<std::pair<size_t, size_t>, bool> values;
    for (const auto& row : rows_) {
      values[{row.file, row.entry}] = row.check->GetValue();
    }
    return values;
  }

  void OnSearch(wxCommandEvent&) {
    // Debounced: rebuilds happen in OnSearchTimer so fast typing doesn't
    // reshuffle the page under the user on every keystroke.
    search_timer_.StartOnce(200);
  }

  void OnSearchTimer(wxTimerEvent&) {
    const std::string query =
        ToLowerCopy(TrimCopy(search_->GetValue().ToStdString()));
    if (query == search_query_) {
      return;
    }
    search_query_ = query;
    const auto values = CollectValues();
    BuildPages(&values);
    book_->InvalidateBestSize();
    Layout();
  }

  void OnSave(wxCommandEvent&) {
    // Group desired states per file; rewrite only files with changes. Files
    // save one at a time, so track what already landed to report accurately
    // if a later file fails.
    std::vector<std::string> saved;
    const auto saved_text = [&saved]() {
      if (saved.empty()) {
        return std::string();
      }
      std::string names;
      for (const auto& name : saved) {
        names += (names.empty() ? "" : ", ") + name;
      }
      return "Already saved in this run: " + names + ".\n\n";
    };
    for (size_t f = 0; f < files_.size(); ++f) {
      std::vector<std::pair<size_t, bool>> desired;
      for (const auto& row : rows_) {
        if (row.file != f) {
          continue;
        }
        const bool enabled = row.check->GetValue();
        if (enabled != files_[f].parsed.patch_info[row.entry].is_enabled) {
          desired.emplace_back(row.entry, enabled);
        }
      }
      if (desired.empty()) {
        continue;
      }
      if (!patcher::PatchDB::WriteEnabledFlags(files_[f].path, desired)) {
        wxMessageBox(
            WxLabel("Failed to update " + files_[f].filename + ".\n\n" +
                    saved_text() +
                    "The file was left untouched - check its .bak backup."),
            "Patches", wxOK | wxICON_ERROR, this);
        return;
      }
      // Re-parse to confirm the flags took and refresh the state.
      files_[f].parsed = patcher::PatchDB::ReadPatchFile(files_[f].path);
      for (const auto& [index, enabled] : desired) {
        if (index >= files_[f].parsed.patch_info.size() ||
            files_[f].parsed.patch_info[index].is_enabled != enabled) {
          wxMessageBox(WxLabel("Verification failed for " + files_[f].filename +
                               ".\n\n" + saved_text() +
                               "Check the file and its .bak backup."),
                       "Patches", wxOK | wxICON_ERROR, this);
          return;
        }
      }
      saved.push_back(files_[f].filename);
    }
    if (saved.empty()) {
      return;
    }
    wxMessageBox(
        "Patches saved.\n\nThey apply the next time this title is "
        "started.",
        "Patches", wxOK | wxICON_INFORMATION, this);
  }

  std::string patches_dir_;
  std::vector<PatchFile> files_;
  std::vector<PatchRow> rows_;
  std::string search_query_;
  wxTreebook* book_ = nullptr;
  wxSearchCtrl* search_ = nullptr;
  wxButton* save_button_ = nullptr;
  wxTimer search_timer_;
};

}  // namespace

void ShowPatchDialog(wxWindow* parent, const std::filesystem::path& patches_dir,
                     const std::string& title_id,
                     const std::string& title_name) {
  if (!parent || title_id.empty()) {
    return;
  }
  WxPatchDialog dialog(parent, patches_dir, title_id,
                       title_name.empty() ? title_id : title_name);
  dialog.ShowModal();
}

}  // namespace wx_ui
}  // namespace app
}  // namespace xe
