/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// Pre-install picker for content packages: checkbox list with icons, plus
// in-dialog Scan Folder / Add Files actions that append without closing.

#include "xenia/app/wx/wx_content_pick_dialog.h"

#include <algorithm>
#include <fstream>
#include <set>

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
#include "xenia/app/wx/wx_window.h"
#include "xenia/app/wx/wx_window_priv.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/vfs/devices/xcontent_container_device.h"
#include "xenia/xbox.h"

namespace xe {
namespace app {
namespace wx_ui {

namespace {

// Modal return code for Extract Selected (Install Selected is wxID_OK).
constexpr int kPickExtract = wxID_HIGHEST + 1;

wxWindow* PickParent(WxWindow* window) {
  wxWindow* parent = window->view() ? static_cast<wxWindow*>(window->view())
                                    : static_cast<wxWindow*>(window->frame());
  return parent;
}

bool HasPackageMagic(const std::filesystem::path& path) {
  std::ifstream file(xe::to_path(xe::path_to_utf8(path)), std::ios::binary);
  if (!file.is_open()) {
    return false;
  }
  char magic[4] = {};
  file.read(magic, sizeof(magic));
  if (file.gcount() != sizeof(magic)) {
    return false;
  }
  fourcc_t sig = make_fourcc(magic[0], magic[1], magic[2], magic[3]);
  return sig == vfs::kCONSignature || sig == vfs::kLIVESignature ||
         sig == vfs::kPIRSSignature;
}

wxBitmap IconBitmap(const std::vector<uint8_t>& bytes, int px) {
  if (bytes.empty()) {
    return wxNullBitmap;
  }
  wxMemoryInputStream stream(bytes.data(), bytes.size());
  wxImage image(stream, wxBITMAP_TYPE_ANY);
  if (!image.IsOk()) {
    return wxNullBitmap;
  }
  if (image.GetWidth() != px || image.GetHeight() != px) {
    image.Rescale(px, px, wxIMAGE_QUALITY_HIGH);
  }
  return wxBitmap(image);
}

wxBitmap PlaceholderBitmap(int px) {
  wxBitmap bitmap(px, px);
  wxMemoryDC dc(bitmap);
  dc.SetBackground(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE)));
  dc.Clear();
  wxFont font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
  font.SetPointSize(font.GetPointSize() * 2);
  font.MakeBold();
  dc.SetFont(font);
  dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
  dc.DrawLabel("?", wxRect(0, 0, px, px),
               wxALIGN_CENTER_HORIZONTAL | wxALIGN_CENTER_VERTICAL);
  dc.SelectObject(wxNullBitmap);
  return bitmap;
}

}  // namespace

std::vector<std::filesystem::path> DiscoverContentPackages(
    const std::filesystem::path& dir) {
  std::vector<std::filesystem::path> found;
  std::error_code ec = {};
  if (!std::filesystem::is_directory(dir, ec)) {
    return found;
  }
  std::error_code rec = {};
  for (auto it = std::filesystem::recursive_directory_iterator(dir, rec);
       it != std::filesystem::recursive_directory_iterator(); ++it) {
    if (rec) {
      break;
    }
    std::error_code ec2 = {};
    if (!it->is_regular_file(ec2)) {
      continue;
    }
    if (HasPackageMagic(it->path())) {
      found.push_back(it->path());
    }
  }
  std::sort(found.begin(), found.end());
  return found;
}

void PrepareContentEntries(WxWindow* window, Emulator* emulator,
                           const std::vector<std::filesystem::path>& paths,
                           std::vector<Emulator::ContentInstallEntry>& out) {
  if (!window || !emulator || paths.empty()) {
    return;
  }
  wxWindow* parent = PickParent(window);
  if (!parent) {
    return;
  }
  // Header reads stay on the UI thread (icon creation is UI-owned); each
  // package is just a header read, so pumping the dialog per file keeps
  // Cancel alive.
  wxProgressDialog progress("Preparing content", "Reading package headers...",
                            int(paths.size()), parent,
                            wxPD_APP_MODAL | wxPD_CAN_ABORT | wxPD_AUTO_HIDE);
  // Stage locally: entries are move-constructible but not move-assignable,
  // so out is only touched on success (erase would require assignment).
  std::vector<Emulator::ContentInstallEntry> added;
  for (size_t i = 0; i < paths.size(); i++) {
    Emulator::ContentInstallEntry entry(paths[i]);
    emulator->ProcessContentPackageHeader(paths[i], entry);
    if (entry.installation_state_.load() != Emulator::InstallState::failed) {
      added.push_back(std::move(entry));
    } else {
      XELOGW("Content: skipping unrecognized package {}",
             xe::path_to_utf8(paths[i]));
    }
    if (!progress.Update(int(i + 1),
                         WxLabel(xe::path_to_utf8(paths[i].filename())))) {
      return;
    }
  }
  progress.Update(int(paths.size()));
  for (auto& entry : added) {
    out.push_back(std::move(entry));
  }
}

class WxContentPickDialog : public wxDialog {
 public:
  WxContentPickDialog(WxWindow* window, Emulator* emulator,
                      std::vector<Emulator::ContentInstallEntry>& entries)
      : wxDialog(PickParent(window), wxID_ANY, "Select Content Packages",
                 wxDefaultPosition, wxDefaultSize,
                 wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
        window_(window),
        emulator_(emulator),
        entries_(entries) {
    auto* outer = new wxBoxSizer(wxVERTICAL);
    auto* header = new wxBoxSizer(wxHORIZONTAL);
    header->Add(
        new wxStaticText(this, wxID_ANY,
                         WxLabel("Select the content packages to install or "
                                 "extract:")),
        0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    header->AddStretchSpacer(1);
    auto* scan_folder = new wxButton(this, wxID_ANY, "Scan Folder...");
    scan_folder->Bind(wxEVT_BUTTON, &WxContentPickDialog::OnScanFolder, this);
    header->Add(scan_folder, 0, wxRIGHT, FromDIP(8));
    auto* add_files = new wxButton(this, wxID_ANY, "Add Files...");
    add_files->Bind(wxEVT_BUTTON, &WxContentPickDialog::OnAddFiles, this);
    header->Add(add_files, 0);
    outer->Add(header, 0, wxEXPAND | wxALL, FromDIP(10));

    scrolled_ = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition,
                                     wxDefaultSize, wxVSCROLL);
    list_ = new wxBoxSizer(wxVERTICAL);
    for (size_t i = 0; i < entries_.size(); i++) {
      AddEntryRow(i);
    }
    scrolled_->SetSizer(list_);
    scrolled_->SetScrollRate(0, FromDIP(10));
    scrolled_->SetMinSize(FromDIP(wxSize(620, 360)));
    outer->Add(scrolled_, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(10));

    auto* bottom = new wxBoxSizer(wxHORIZONTAL);
    auto* select_all = new wxButton(this, wxID_ANY, "Select All");
    select_all->Bind(wxEVT_BUTTON, &WxContentPickDialog::OnSelectAll, this);
    bottom->Add(select_all, 0, wxRIGHT, FromDIP(8));
    auto* select_none = new wxButton(this, wxID_ANY, "Select None");
    select_none->Bind(wxEVT_BUTTON, &WxContentPickDialog::OnSelectNone, this);
    bottom->Add(select_none, 0);
    bottom->AddStretchSpacer(1);
    auto* install = new wxButton(this, wxID_OK, "Install Selected");
    install->SetDefault();
    bottom->Add(install, 0, wxRIGHT, FromDIP(8));
    auto* extract = new wxButton(this, kPickExtract, "Extract Selected");
    extract->Bind(wxEVT_BUTTON, &WxContentPickDialog::OnExtract, this);
    bottom->Add(extract, 0, wxRIGHT, FromDIP(8));
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

  const ContentPickResult& result() const { return result_; }

  std::vector<size_t> CheckedIndices() const {
    std::vector<size_t> checked;
    for (size_t i = 0; i < checks_.size() && i < indices_.size(); i++) {
      if (checks_[i]->GetValue()) {
        checked.push_back(indices_[i]);
      }
    }
    return checked;
  }

 private:
  void AddEntryRow(size_t index) {
    const auto& entry = entries_[index];
    // Exact-path dedup for in-dialog additions.
    if (!shown_.insert(entry.path_).second) {
      return;
    }
    indices_.push_back(index);
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    auto* check = new wxCheckBox(scrolled_, wxID_ANY, wxString());
    check->SetValue(true);
    checks_.push_back(check);
    row->Add(check, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    const int icon_px = FromDIP(64);
    wxBitmap icon_art = IconBitmap(entry.icon_bytes_, icon_px);
    if (!icon_art.IsOk()) {
      icon_art = PlaceholderBitmap(icon_px);
    }
    auto* icon = new wxStaticBitmap(scrolled_, wxID_ANY, icon_art);
    icon->SetMinSize(FromDIP(wxSize(64, 64)));
    row->Add(icon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    auto* col = new wxBoxSizer(wxVERTICAL);
    auto* name = new wxStaticText(scrolled_, wxID_ANY, WxLabel(entry.name_));
    wxFont bold = name->GetFont();
    bold.SetWeight(wxFONTWEIGHT_BOLD);
    name->SetFont(bold);
    col->Add(name, 0, wxEXPAND | wxBOTTOM, FromDIP(2));
    if (entry.content_type_ != xe::XContentType::kInvalid) {
      col->Add(new wxStaticText(
                   scrolled_, wxID_ANY,
                   WxLabel("Content Type: " +
                           xe::XContentTypeMap.at(entry.content_type_))),
               0, wxEXPAND | wxBOTTOM, FromDIP(2));
    }
    const std::string location = xe::path_to_utf8(entry.path_);
    auto* path_text =
        new wxStaticText(scrolled_, wxID_ANY, WxLabel("Path: " + location));
    path_text->SetToolTip(WxLabel(location));
    col->Add(path_text, 0, wxEXPAND);
    row->Add(col, 1, wxALIGN_CENTER_VERTICAL);
    list_->Add(row, 0, wxEXPAND | wxALL, FromDIP(4));
  }

  void AppendPaths(const std::vector<std::filesystem::path>& paths,
                   Emulator* emulator) {
    if (paths.empty()) {
      return;
    }
    const size_t from = entries_.size();
    PrepareContentEntries(window_, emulator, paths, entries_);
    for (size_t i = from; i < entries_.size(); i++) {
      AddEntryRow(i);
    }
    scrolled_->FitInside();
    scrolled_->Layout();
    Layout();
  }

  void OnSelectAll(wxCommandEvent&) { SetAll(true); }
  void OnSelectNone(wxCommandEvent&) { SetAll(false); }

  void OnScanFolder(wxCommandEvent&) {
    wxDirDialog dir_dialog(this, "Scan Folder for Content Packages");
    if (dir_dialog.ShowModal() != wxID_OK) {
      return;
    }
    AppendPaths(DiscoverContentPackages(WxToPath(dir_dialog.GetPath())),
                emulator_);
  }

  void OnAddFiles(wxCommandEvent&) {
    wxFileDialog file_dialog(this, "Select Content Package", wxString(),
                             wxString(), "All files (*.*)|*.*",
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
    AppendPaths(paths, emulator_);
  }

  void OnExtract(wxCommandEvent&) {
    wxDirDialog dir_dialog(this, "Select Directory to Extract");
    if (dir_dialog.ShowModal() != wxID_OK) {
      return;
    }
    result_.extract_dir = WxToPath(dir_dialog.GetPath());
    result_.checked = CheckedIndices();
    result_.action = ContentPickAction::kExtract;
    EndModal(kPickExtract);
  }

  void SetAll(bool value) {
    for (auto* check : checks_) {
      check->SetValue(value);
    }
  }

  WxWindow* window_ = nullptr;
  Emulator* emulator_ = nullptr;
  std::vector<Emulator::ContentInstallEntry>& entries_;
  wxScrolledWindow* scrolled_ = nullptr;
  wxBoxSizer* list_ = nullptr;
  std::vector<wxCheckBox*> checks_;
  // Entry indices backing the rows (AddEntryRow skips duplicates).
  std::vector<size_t> indices_;
  std::set<std::filesystem::path> shown_;
  ContentPickResult result_;
};

ContentPickResult ShowContentPickDialog(
    WxWindow* window, Emulator* emulator,
    std::vector<Emulator::ContentInstallEntry>& entries) {
  ContentPickResult cancelled;
  if (!window || !emulator || entries.empty()) {
    return cancelled;
  }
  WxContentPickDialog dialog(window, emulator, entries);
  const int code = dialog.ShowModal();
  if (code == kPickExtract) {
    return dialog.result();
  }
  if (code == wxID_OK) {
    // Install Selected closes via the stock OK button.
    ContentPickResult result;
    result.action = ContentPickAction::kInstall;
    result.checked = dialog.CheckedIndices();
    return result;
  }
  return cancelled;
}

}  // namespace wx_ui
}  // namespace app
}  // namespace xe
