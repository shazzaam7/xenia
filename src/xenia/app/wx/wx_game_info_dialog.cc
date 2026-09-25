/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/app/wx/wx_game_info_dialog.h"

#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/filedlg.h>
#include <wx/hyperlink.h>
#include <wx/image.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include "xenia/app/wx/wx_compat_db.h"
#include "xenia/app/wx/wx_game_art.h"
#include "xenia/app/wx/wx_game_scan.h"
#include "xenia/app/wx/wx_library_store.h"
#include "xenia/app/wx/wx_util.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"

namespace xe {
namespace app {
namespace wx_ui {

namespace {

bool ReadPickedBytes(const std::filesystem::path& path,
                     std::vector<uint8_t>& bytes_out) {
  bytes_out.clear();
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return false;
  }
  bytes_out.assign(std::istreambuf_iterator<char>(file),
                   std::istreambuf_iterator<char>());
  return !bytes_out.empty();
}

std::string LastPlayedLabel(std::time_t t) {
  if (!t) {
    return "Never";
  }
  std::tm tm = {};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  std::ostringstream ss;
  ss << std::put_time(&tm, "%Y-%m-%d %H:%M");
  return ss.str();
}

std::string NormalizeHex(std::string s) {
  for (char& c : s) {
    c = char(std::toupper(static_cast<unsigned char>(c)));
  }
  return s;
}

}  // namespace

class WxGameInfoDialog : public wxDialog {
 public:
  WxGameInfoDialog(wxWindow* parent, const std::filesystem::path& storage_root,
                   GameEntry entry)
      : wxDialog(
            parent, wxID_ANY,
            WxLabel("Game Info - " + entry.name + " (" + entry.title_id + ")"),
            wxDefaultPosition, wxDefaultSize,
            wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
        storage_root_(storage_root),
        entry_(std::move(entry)) {
    auto* outer = new wxBoxSizer(wxVERTICAL);
    BuildHeader(outer);
    BuildCompat(outer);
    BuildDiscsPanel(outer);
    auto* footnote = new wxStaticText(
        this, wxID_ANY,
        "Disc order sets boot-selection order. Artwork changes apply "
        "immediately, even on Cancel.");
    footnote->Enable(false);
    outer->Add(footnote, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(8));

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    buttons->AddStretchSpacer(1);
    save_button_ = new wxButton(this, wxID_SAVE, "Save");
    save_button_->SetDefault();
    buttons->Add(save_button_, 0, wxRIGHT | wxBOTTOM, FromDIP(8));
    buttons->Add(new wxButton(this, wxID_CANCEL), 0, wxRIGHT | wxBOTTOM,
                 FromDIP(8));
    outer->Add(buttons, 0, wxEXPAND | wxTOP, FromDIP(8));
    SetSizer(outer);
    Fit();
    // Big enough to show the header, compat, and one disc form without
    // scrolling; small enough for 1080p displays.
    wxSize size = GetSize();
    size.IncTo(FromDIP(wxSize(660, 560)));
    size.x = std::min(size.x, FromDIP(720));
    size.y = std::min(size.y, FromDIP(740));
    SetSize(size);

    save_button_->Bind(wxEVT_BUTTON, &WxGameInfoDialog::OnSave, this);
  }

  const GameEntry& entry() const { return entry_; }
  bool art_changed() const { return art_changed_; }

 private:
  void BuildHeader(wxBoxSizer* outer) {
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    icon_bitmap_ = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap);
    row->Add(icon_bitmap_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    auto* col = new wxBoxSizer(wxVERTICAL);
    name_ = new wxTextCtrl(this, wxID_ANY, WxLabel(entry_.name));
    name_->SetMinSize(wxSize(FromDIP(260), -1));
    col->Add(name_, 0, wxEXPAND | wxBOTTOM, FromDIP(4));
    col->Add(new wxStaticText(this, wxID_ANY,
                              WxLabel("Title ID: " + entry_.title_id)),
             0, wxBOTTOM, FromDIP(4));
    col->Add(new wxStaticText(
                 this, wxID_ANY,
                 WxLabel("Last played: " + LastPlayedLabel(entry_.last_play))),
             0);
    row->Add(col, 1, wxEXPAND);
    outer->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));
    // Full-width row: the three buttons never fit beside the name field.
    auto* art_buttons = new wxBoxSizer(wxHORIZONTAL);
    auto* change_icon = new wxButton(this, wxID_ANY, "Change Icon...");
    change_icon->Bind(wxEVT_BUTTON, &WxGameInfoDialog::OnChangeIcon, this);
    art_buttons->Add(change_icon, 0, wxRIGHT, FromDIP(8));
    auto* change_bg = new wxButton(this, wxID_ANY, "Change Background...");
    change_bg->Bind(wxEVT_BUTTON, &WxGameInfoDialog::OnChangeBackground, this);
    art_buttons->Add(change_bg, 0, wxRIGHT, FromDIP(8));
    auto* reset_art = new wxButton(this, wxID_ANY, "Reset Artwork");
    reset_art->Bind(wxEVT_BUTTON, &WxGameInfoDialog::OnResetArtwork, this);
    art_buttons->Add(reset_art, 0);
    outer->Add(art_buttons, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));
    RefreshArtPreviews();
  }

  void BuildCompat(wxBoxSizer* outer) {
    auto* box = new wxStaticBoxSizer(wxVERTICAL, this, "Compatibility");
    const CompatRating rating = CompatRatingFromId(entry_.compat.state);
    auto* status =
        new wxStaticText(box->GetStaticBox(), wxID_ANY, CompatName(rating));
    wxFont bold = status->GetFont();
    bold.SetWeight(wxFONTWEIGHT_BOLD);
    status->SetFont(bold);
    status->SetForegroundColour(CompatColor(rating));
    box->Add(status, 0, wxALL, FromDIP(4));
    if (!entry_.compat.url.empty()) {
      box->Add(new wxHyperlinkCtrl(box->GetStaticBox(), wxID_ANY, "View report",
                                   WxLabel(entry_.compat.url)),
               0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(4));
    } else {
      auto* none = new wxStaticText(box->GetStaticBox(), wxID_ANY,
                                    "No compatibility report.");
      none->Enable(false);
      box->Add(none, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(4));
    }
    outer->Add(box, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(8));
  }

  void BuildDiscsPanel(wxBoxSizer* outer) {
    auto* box = new wxStaticBoxSizer(wxVERTICAL, this, "Discs");
    auto* selector = new wxBoxSizer(wxHORIZONTAL);
    selector->Add(new wxStaticText(box->GetStaticBox(), wxID_ANY, "Disc:"), 0,
                  wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
    disc_choice_ = new wxChoice(box->GetStaticBox(), wxID_ANY);
    disc_choice_->Bind(wxEVT_CHOICE, &WxGameInfoDialog::OnSelectDisc, this);
    selector->Add(disc_choice_, 1, wxEXPAND | wxRIGHT, FromDIP(4));
    disc_up_ = new wxButton(box->GetStaticBox(), wxID_ANY, "Up");
    disc_up_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnMoveDisc(-1); });
    selector->Add(disc_up_, 0, wxRIGHT, FromDIP(4));
    disc_down_ = new wxButton(box->GetStaticBox(), wxID_ANY, "Down");
    disc_down_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnMoveDisc(1); });
    selector->Add(disc_down_, 0, wxRIGHT, FromDIP(4));
    disc_remove_ = new wxButton(box->GetStaticBox(), wxID_ANY, "Remove");
    disc_remove_->Bind(wxEVT_BUTTON, &WxGameInfoDialog::OnRemoveDisc, this);
    selector->Add(disc_remove_, 0);
    box->Add(selector, 0, wxEXPAND | wxALL, FromDIP(4));

    auto* form = box->GetStaticBox();
    auto* label_row = new wxBoxSizer(wxHORIZONTAL);
    label_row->Add(new wxStaticText(form, wxID_ANY, "Label:"), 0,
                   wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
    disc_label_ = new wxTextCtrl(form, wxID_ANY, wxString());
    disc_label_->SetMinSize(wxSize(FromDIP(220), -1));
    label_row->Add(disc_label_, 1, wxEXPAND);
    box->Add(label_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(4));

    disc_version_ = new wxStaticText(form, wxID_ANY, wxString());
    box->Add(disc_version_, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(4));

    disc_media_ = new wxStaticText(form, wxID_ANY, wxString());
    box->Add(disc_media_, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(4));

    auto* path_row = new wxBoxSizer(wxHORIZONTAL);
    path_row->Add(new wxStaticText(form, wxID_ANY, "Path:"), 0,
                  wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
    disc_path_ = new wxTextCtrl(form, wxID_ANY, wxString());
    disc_path_->SetMinSize(wxSize(FromDIP(300), -1));
    path_row->Add(disc_path_, 1, wxEXPAND | wxRIGHT, FromDIP(4));
    auto* browse = new wxButton(form, wxID_ANY, "Browse...");
    browse->Bind(wxEVT_BUTTON, &WxGameInfoDialog::OnBrowseDisc, this);
    path_row->Add(browse, 0, wxRIGHT, FromDIP(4));
    auto* refresh = new wxButton(form, wxID_ANY, "Refresh");
    refresh->Bind(wxEVT_BUTTON, &WxGameInfoDialog::OnRefreshDisc, this);
    path_row->Add(refresh, 0);
    box->Add(path_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(4));

    auto* add = new wxButton(box->GetStaticBox(), wxID_ANY, "Add Disc...");
    add->Bind(wxEVT_BUTTON, &WxGameInfoDialog::OnAddDisc, this);
    box->Add(add, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(4));
    outer->Add(box, 0, wxEXPAND | wxALL, FromDIP(8));
    LoadDiscForm();
  }

  // Writes the visible form back into the selected disc (media/version are
  // display-only and untouched).
  void SyncDiscForm() {
    if (selected_ < entry_.discs.size()) {
      entry_.discs[selected_].label = disc_label_->GetValue().ToStdString();
      entry_.discs[selected_].path = WxToPath(disc_path_->GetValue());
    }
  }

  void UpdateDiscChoice() {
    if (!entry_.discs.empty() && selected_ >= entry_.discs.size()) {
      selected_ = entry_.discs.size() - 1;
    }
    disc_choice_->Clear();
    for (const auto& disc : entry_.discs) {
      disc_choice_->Append("Disc " + std::to_string(disc.number) + " of " +
                           std::to_string(entry_.discs.size()) + " - " +
                           disc.label);
    }
    if (!entry_.discs.empty()) {
      disc_choice_->SetSelection(int(selected_));
    }
    const bool multi = entry_.discs.size() > 1;
    disc_up_->Enable(multi && selected_ > 0);
    disc_down_->Enable(multi && selected_ + 1 < entry_.discs.size());
    disc_remove_->Enable(multi);
  }

  void LoadDiscForm() {
    UpdateDiscChoice();
    if (selected_ >= entry_.discs.size()) {
      return;
    }
    const auto& disc = entry_.discs[selected_];
    disc_label_->ChangeValue(WxLabel(disc.label));
    disc_label_->SetBackgroundColour(wxNullColour);
    disc_version_->SetLabel(WxLabel(disc.version.empty()
                                        ? "Version: unknown"
                                        : "Version: " + disc.version));
    disc_media_->SetLabel(WxLabel("Media ID: " + disc.media_id));
    const auto path_text = WxLabel(xe::path_to_utf8(disc.path));
    disc_path_->ChangeValue(path_text);
    disc_path_->SetToolTip(path_text);
    disc_path_->SetBackgroundColour(wxNullColour);
    disc_path_->Refresh();
  }

  void Renumber() {
    for (size_t i = 0; i < entry_.discs.size(); i++) {
      entry_.discs[i].number = int(i) + 1;
    }
    if (entry_.last_played_disc > int(entry_.discs.size())) {
      entry_.last_played_disc = 1;
    }
    if (entry_.last_played_disc < 1) {
      entry_.last_played_disc = 1;
    }
  }

  // Re-reads one disc file for its media/version. False with a message when
  // the file is unreadable or belongs to another title.
  bool RefreshDiscMeta(size_t pos) {
    if (pos >= entry_.discs.size()) {
      return false;
    }
    auto& disc = entry_.discs[pos];
    GameMeta meta;
    if (!ReadGameMeta(disc.path, meta) || !meta.ok ||
        NormalizeHex(meta.title_id) != NormalizeHex(entry_.title_id)) {
      wxMessageBox(
          "Could not read a matching game file there.\nExpected title " +
              WxLabel(entry_.title_id) + ".",
          "Refresh disc", wxOK | wxICON_ERROR, this);
      return false;
    }
    disc.media_id = meta.media_id;
    disc.version = meta.version;
    return true;
  }

  void OnSelectDisc(wxCommandEvent&) {
    SyncDiscForm();
    selected_ = size_t((std::max)(0, disc_choice_->GetSelection()));
    LoadDiscForm();
  }

  void OnMoveDisc(int dir) {
    SyncDiscForm();
    const int other = int(selected_) + dir;
    if (selected_ >= entry_.discs.size() || other < 0 ||
        size_t(other) >= entry_.discs.size()) {
      return;
    }
    std::swap(entry_.discs[selected_], entry_.discs[size_t(other)]);
    selected_ = size_t(other);
    Renumber();
    LoadDiscForm();
  }

  void OnRemoveDisc(wxCommandEvent&) {
    if (entry_.discs.size() <= 1) {
      wxMessageBox(
          "A title needs at least one disc.\nRemove the game instead to "
          "delete the whole title.",
          "Remove disc", wxOK | wxICON_INFORMATION, this);
      return;
    }
    SyncDiscForm();
    if (selected_ < entry_.discs.size()) {
      entry_.discs.erase(entry_.discs.begin() + selected_);
    }
    if (!entry_.discs.empty() && selected_ >= entry_.discs.size()) {
      selected_ = entry_.discs.size() - 1;
    }
    Renumber();
    LoadDiscForm();
  }

  void OnRefreshDisc(wxCommandEvent&) {
    SyncDiscForm();
    if (selected_ >= entry_.discs.size()) {
      return;
    }
    if (RefreshDiscMeta(selected_)) {
      LoadDiscForm();
    } else {
      disc_path_->SetBackgroundColour(ErrorBgColour());
      disc_path_->Refresh();
    }
  }

  void OnBrowseDisc(wxCommandEvent&) {
    wxFileDialog dialog(
        this, "Select Disc File", wxString(), wxString(),
        "Xbox 360 games (*.xex;*.iso;*.xiso;*.zar)|*.xex;*.iso;*.xiso;*.zar|"
        "All files (*.*)|*.*",
        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK) {
      return;
    }
    disc_path_->SetValue(dialog.GetPath());
    disc_path_->SetBackgroundColour(wxNullColour);
    disc_path_->Refresh();
    // Resolve the new file immediately so media/version follow the path.
    SyncDiscForm();
    wxCommandEvent dummy;
    OnRefreshDisc(dummy);
  }

  void OnAddDisc(wxCommandEvent&) {
    wxFileDialog dialog(
        this, "Add Disc", wxString(), wxString(),
        "Xbox 360 games (*.xex;*.iso;*.xiso;*.zar)|*.xex;*.iso;*.xiso;*.zar|"
        "All files (*.*)|*.*",
        wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_MULTIPLE);
    if (dialog.ShowModal() != wxID_OK) {
      return;
    }
    SyncDiscForm();
    const size_t discs_before = entry_.discs.size();
    wxArrayString wx_paths;
    dialog.GetPaths(wx_paths);
    std::vector<std::string> skipped;
    for (const auto& p : wx_paths) {
      const auto path = WxToPath(p);
      GameMeta meta;
      if (!ReadGameMeta(path, meta) || !meta.ok ||
          NormalizeHex(meta.title_id) != NormalizeHex(entry_.title_id)) {
        skipped.push_back(xe::path_to_utf8(path.filename()));
        continue;
      }
      std::string name =
          meta.name.empty() ? xe::path_to_utf8(path.stem()) : meta.name;
      std::vector<GameEntry> holder = {entry_};
      if (MergeScannedGame(holder, path, meta.title_id, meta.media_id,
                           meta.version, name)) {
        entry_ = std::move(holder[0]);
      } else {
        skipped.push_back(xe::path_to_utf8(path.filename()) +
                          " (already listed)");
      }
    }
    if (entry_.discs.size() > discs_before) {
      // New disc appended: show it.
      selected_ = entry_.discs.size() - 1;
    }
    Renumber();
    LoadDiscForm();
    if (!skipped.empty()) {
      std::string message =
          "Skipped files (unreadable, another title, or "
          "already listed):\n";
      for (const auto& s : skipped) {
        message += s + "\n";
      }
      wxMessageBox(WxLabel(message), "Add disc", wxOK | wxICON_WARNING, this);
    }
  }

  void RefreshArtPreviews() {
    std::error_code ec = {};
    auto icon_path = ArtIconPath(storage_root_, entry_.title_id);
    if (std::filesystem::exists(icon_path, ec)) {
      wxImage image;
      if (image.LoadFile(wxString::FromUTF8(xe::path_to_utf8(icon_path))) &&
          image.IsOk()) {
        const int px = FromDIP(64);
        icon_bitmap_->SetBitmap(
            wxBitmap(image.Scale(px, px, wxIMAGE_QUALITY_HIGH)));
      }
    } else {
      icon_bitmap_->SetBitmap(wxNullBitmap);
    }
    Layout();
  }

  void OnChangeIcon(wxCommandEvent&) {
    wxFileDialog dialog(this, "Choose Icon", wxString(), wxString(),
                        "Images (*.png;*.jpg;*.jpeg)|*.png;*.jpg;*.jpeg",
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK) {
      return;
    }
    std::vector<uint8_t> bytes;
    if (!ReadPickedBytes(WxToPath(dialog.GetPath()), bytes) ||
        !SaveIconFile(bytes, ArtIconPath(storage_root_, entry_.title_id))) {
      wxMessageBox("That file is not a readable PNG or JPEG.", "Change icon",
                   wxOK | wxICON_ERROR, this);
      return;
    }
    art_changed_ = true;
    RefreshArtPreviews();
  }

  void OnChangeBackground(wxCommandEvent&) {
    wxFileDialog dialog(this, "Choose Background", wxString(), wxString(),
                        "Images (*.png;*.jpg;*.jpeg)|*.png;*.jpg;*.jpeg",
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK) {
      return;
    }
    std::vector<uint8_t> bytes;
    if (!ReadPickedBytes(WxToPath(dialog.GetPath()), bytes) ||
        !SaveBackgroundFile(
            bytes, ArtBackgroundPath(storage_root_, entry_.title_id))) {
      wxMessageBox("That file is not a readable PNG or JPEG.",
                   "Change background", wxOK | wxICON_ERROR, this);
      return;
    }
    art_changed_ = true;
    RefreshArtPreviews();
  }

  void OnResetArtwork(wxCommandEvent&) {
    std::error_code ec = {};
    std::filesystem::remove(ArtIconPath(storage_root_, entry_.title_id), ec);
    std::filesystem::remove(ArtIconAltPath(storage_root_, entry_.title_id), ec);
    std::filesystem::remove(ArtBackgroundPath(storage_root_, entry_.title_id),
                            ec);
    if (!entry_.discs.empty()) {
      GameMeta meta;
      if (ReadGameMeta(entry_.discs[0].path, meta) && meta.ok) {
        EnsureArtwork(storage_root_, entry_.discs[0].path, meta.type,
                      meta.icon_bytes, entry_.title_id);
      }
    }
    art_changed_ = true;
    RefreshArtPreviews();
  }

  void OnSave(wxCommandEvent&) {
    SyncDiscForm();
    entry_.name = name_->GetValue().ToStdString();
    name_->SetBackgroundColour(wxNullColour);
    name_->Refresh();
    disc_label_->SetBackgroundColour(wxNullColour);
    disc_label_->Refresh();
    disc_path_->SetBackgroundColour(wxNullColour);
    disc_path_->Refresh();
    bool valid = true;
    std::string other_issues;
    if (entry_.name.empty()) {
      name_->SetBackgroundColour(ErrorBgColour());
      name_->Refresh();
      valid = false;
    }
    for (size_t i = 0; i < entry_.discs.size(); i++) {
      std::error_code ec = {};
      const bool label_ok = !entry_.discs[i].label.empty();
      const bool path_ok = !entry_.discs[i].path.empty() &&
                           std::filesystem::exists(entry_.discs[i].path, ec);
      if (label_ok && path_ok) {
        continue;
      }
      valid = false;
      if (i == selected_) {
        if (!label_ok) {
          disc_label_->SetBackgroundColour(ErrorBgColour());
          disc_label_->Refresh();
        }
        if (!path_ok) {
          disc_path_->SetBackgroundColour(ErrorBgColour());
          disc_path_->Refresh();
        }
      } else {
        other_issues += "Disc " + std::to_string(entry_.discs[i].number);
        other_issues += (!label_ok && !path_ok) ? " (label and path)\n"
                        : !label_ok             ? " (label)\n"
                                                : " (path)\n";
      }
    }
    if (!valid) {
      std::string message =
          "Names and labels cannot be empty, and every "
          "disc path must exist.";
      if (!other_issues.empty()) {
        message += "\nOther discs needing attention:\n" + other_issues;
      }
      wxMessageBox(WxLabel(message), "Save game info", wxOK | wxICON_WARNING,
                   this);
      return;
    }
    Renumber();
    EndModal(wxID_SAVE);
  }

  std::filesystem::path storage_root_;
  GameEntry entry_;
  bool art_changed_ = false;
  wxTextCtrl* name_ = nullptr;
  wxStaticBitmap* icon_bitmap_ = nullptr;
  // Disc selector: one disc form visible at a time.
  wxChoice* disc_choice_ = nullptr;
  wxTextCtrl* disc_label_ = nullptr;
  wxStaticText* disc_version_ = nullptr;
  wxStaticText* disc_media_ = nullptr;
  wxTextCtrl* disc_path_ = nullptr;
  wxButton* disc_up_ = nullptr;
  wxButton* disc_down_ = nullptr;
  wxButton* disc_remove_ = nullptr;
  size_t selected_ = 0;
  wxButton* save_button_ = nullptr;
};

bool ShowGameInfoDialog(wxWindow* parent,
                        const std::filesystem::path& storage_root,
                        GameEntry* entry) {
  if (!parent || !entry || entry->title_id.empty() || entry->discs.empty()) {
    return false;
  }
  WxGameInfoDialog dialog(parent, storage_root, *entry);
  if (dialog.ShowModal() != wxID_SAVE) {
    return dialog.art_changed();
  }
  *entry = dialog.entry();
  return true;
}

}  // namespace wx_ui
}  // namespace app
}  // namespace xe
