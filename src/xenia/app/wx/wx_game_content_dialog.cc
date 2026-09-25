/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// Modal per-game content dialog: Achievements / Saves / Title Updates /
// Marketplace tabs. Widget code only; achievement naming/secrecy mirrors
// kernel::xam::ui::GameAchievementsUI.

#include "xenia/app/wx/wx_game_content_dialog.h"

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/dcmemory.h>
#include <wx/dialog.h>
#include <wx/image.h>
#include <wx/imaglist.h>
#include <wx/listctrl.h>
#include <wx/menu.h>
#include <wx/mstream.h>
#include <wx/notebook.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/utils.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <vector>

#include "xenia/app/wx/wx_util.h"
#include "xenia/base/chrono.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/string.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xam/achievement_manager.h"
#include "xenia/kernel/xam/content_manager.h"
#include "xenia/kernel/xam/profile_manager.h"
#include "xenia/kernel/xam/xam_state.h"
#include "xenia/kernel/xam/xcontent/xcontent.h"
#include "xenia/kernel/xam/xcontent/xcontent_package.h"
#include "xenia/kernel/xam/xdbf/gpd_info.h"
#include "xenia/kernel/xam/xdbf/gpd_info_title.h"
#include "xenia/xbox.h"

namespace xe {
namespace app {
namespace wx_ui {

namespace {

constexpr int kIconPx = 64;
constexpr uint32_t kShowUnachieved =
    static_cast<uint32_t>(kernel::xam::AchievementFlags::kShowUnachieved);

std::vector<uint8_t> ReadFileBytes(const std::filesystem::path& path,
                                   size_t max_size = 64 * 1024 * 1024) {
  std::error_code ec = {};
  const auto size = std::filesystem::file_size(path, ec);
  if (ec || !size || size > max_size) {
    return {};
  }
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return {};
  }
  std::vector<uint8_t> bytes;
  bytes.resize(size_t(size));
  file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(size));
  bytes.resize(size_t(file.gcount()));
  return bytes;
}

wxBitmap BitmapFromBytes(const std::vector<uint8_t>& bytes, bool grayscale,
                         int px) {
  if (bytes.empty()) {
    return wxNullBitmap;
  }
  wxMemoryInputStream stream(bytes.data(), bytes.size());
  wxImage image(stream, wxBITMAP_TYPE_ANY);
  if (!image.IsOk()) {
    return wxNullBitmap;
  }
  if (grayscale) {
    image = image.ConvertToGreyscale();
  }
  return wxBitmap(image.Scale(px, px, wxIMAGE_QUALITY_HIGH));
}

// Blank tile for unlocked achievements without image data.
wxBitmap BlankBitmap(int px) {
  wxBitmap bitmap(px, px, 32);
  wxMemoryDC dc(bitmap);
  dc.SetBackground(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE)));
  dc.Clear();
  dc.SelectObject(wxNullBitmap);
  return bitmap;
}

// "?" tile for locked achievements without image data (same idiom as the
// library/install placeholders).
wxBitmap QuestionBitmap(int px) {
  wxBitmap bitmap(px, px, 32);
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

// Checkmark tile for unlocked achievements without image data. Drawn
// proportionally so it scales with the icon size (identical at 64px).
wxBitmap CheckBitmap(int px) {
  wxBitmap bitmap(px, px, 32);
  wxMemoryDC dc(bitmap);
  dc.SetBackground(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE)));
  dc.Clear();
  dc.SetPen(wxPen(wxColour(0x30, 0xA0, 0x30), std::max(1, px * 6 / 64),
                  wxPENSTYLE_SOLID));
  const int h = px / 2;
  dc.DrawLine(h + px * -14 / 64, h + px * 1 / 64, h + px * -4 / 64,
              h + px * 11 / 64);
  dc.DrawLine(h + px * -4 / 64, h + px * 11 / 64, h + px * 15 / 64,
              h + px * -12 / 64);
  dc.SelectObject(wxNullBitmap);
  return bitmap;
}

struct AchRow {
  kernel::xam::Achievement achievement;
  std::vector<uint8_t> icon;
};

// Which text to show for achievements: per-state natural (Default),
// always the unlocked text (Unlocked), or always the locked text (Locked).
enum class AchInfoMode { kDefault = 0, kUnlocked, kLocked };

std::string AchTitle(const AchRow& row, AchInfoMode mode) {
  if (mode == AchInfoMode::kUnlocked || row.achievement.IsUnlocked() ||
      (row.achievement.flags & kShowUnachieved)) {
    return xe::to_utf8(row.achievement.achievement_name);
  }
  return "Secret trophy";
}

std::string AchDescription(const AchRow& row, AchInfoMode mode) {
  if (mode == AchInfoMode::kUnlocked ||
      (mode == AchInfoMode::kDefault && row.achievement.IsUnlocked())) {
    return xe::to_utf8(row.achievement.unlocked_description);
  }
  if (row.achievement.flags & kShowUnachieved) {
    return xe::to_utf8(row.achievement.locked_description);
  }
  return "Hidden description";
}

std::string AchUnlockedTime(const AchRow& row) {
  if (!row.achievement.IsUnlocked()) {
    return "";
  }
  // Format the date on its own first: the full "Unlocked: Offline (YYYY-MM-DD
  // HH:MM)" string does not fit in strftime buffers of a sane size.
  std::string date;
  if (row.achievement.unlock_time.is_valid()) {
    const std::time_t t =
        std::chrono::system_clock::to_time_t(chrono::WinSystemClock::to_sys(
            row.achievement.unlock_time.to_time_point()));
    std::tm tm = {};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
    date = buf;
  }
  if (row.achievement.IsUnlockedOnline()) {
    return date.empty() ? "Unlocked: Online" : "Unlocked: Online " + date;
  }
  if (date.empty()) {
    return "Unlocked: Offline";
  }
  return "Unlocked: Offline (" + date + ")";
}

// Profile package dir holding the loose GPD files (directory packages, which
// is what CreateProfile always makes).
std::filesystem::path ProfilePackageDir(
    const std::filesystem::path& content_root, uint64_t xuid) {
  char xuid_hex[17];
  std::snprintf(xuid_hex, sizeof(xuid_hex), "%016llX",
                (unsigned long long)xuid);
  return content_root / xuid_hex / "FFFE07D1" / "00010000" / xuid_hex;
}

std::string FormatBytes(uint64_t size) {
  char buf[32];
  if (size >= 1024ull * 1024 * 1024) {
    std::snprintf(buf, sizeof(buf), "%.1f GB",
                  double(size) / (1024 * 1024 * 1024));
  } else if (size >= 1024 * 1024) {
    std::snprintf(buf, sizeof(buf), "%.1f MB", double(size) / (1024 * 1024));
  } else if (size >= 1024) {
    std::snprintf(buf, sizeof(buf), "%.1f KB", double(size) / 1024);
  } else {
    std::snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)size);
  }
  return buf;
}

std::string FormatFileTime(const std::filesystem::file_time_type& ft) {
  const auto now_sys = std::chrono::system_clock::now();
  const auto now_file = std::filesystem::file_time_type::clock::now();
  std::time_t t = std::chrono::system_clock::to_time_t(
      std::chrono::time_point_cast<std::chrono::system_clock::duration>(
          ft - now_file + now_sys));
  std::tm tm = {};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
  return buf;
}

// Save dir for one profile+title (kSavedGame only).
std::filesystem::path SaveDir(const std::filesystem::path& content_root,
                              uint64_t xuid, uint32_t title_id) {
  char xuid_hex[17], title_hex[9];
  std::snprintf(xuid_hex, sizeof(xuid_hex), "%016llX",
                (unsigned long long)xuid);
  std::snprintf(title_hex, sizeof(title_hex), "%08X", title_id);
  return content_root / xuid_hex / title_hex / "00000001";
}

// GPD file name inside a profile package dir, e.g. "415607D1.gpd".
std::string GpdFileName(uint32_t title_id) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%08X", title_id);
  return buf + std::string(".gpd");
}

class WxGameContentDialog : public wxDialog {
 public:
  WxGameContentDialog(wxWindow* parent, kernel::KernelState* kernel_state,
                      const std::filesystem::path& content_root,
                      uint32_t title_id, const std::string& title_name)
      : wxDialog(parent, wxID_ANY, WxLabel(title_name + " Content"),
                 wxDefaultPosition, wxDefaultSize,
                 wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
        kernel_state_(kernel_state),
        content_root_(content_root),
        title_id_(title_id) {
    book_ = new wxNotebook(this, wxID_ANY);
    BuildAchievementsPage();
    BuildSavesPage();
    BuildContentPage("Title Updates", "000B0000", tu_);
    BuildContentPage("Marketplace", "00000002", dlc_);
    // Populate before Fit so the dialog is sized to real content (e.g. the
    // gamerscore summary text).
    RefreshAchievements();
    auto* outer = new wxBoxSizer(wxVERTICAL);
    outer->Add(book_, 1, wxEXPAND | wxALL, FromDIP(8));
    auto* close_button = new wxButton(this, wxID_CLOSE, "Close");
    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    buttons->AddStretchSpacer();
    buttons->Add(close_button, 0, wxRIGHT | wxBOTTOM, FromDIP(8));
    outer->Add(buttons, 0, wxEXPAND);
    SetSizer(outer);
    Fit();
    wxSize size = GetSize();
    size.x = std::min(size.x, FromDIP(780));
    size.y = std::min(size.y, FromDIP(620));
    SetSize(size);
    close_button->Bind(
        wxEVT_BUTTON, [this](wxCommandEvent&) { Close(); }, wxID_CLOSE);
  }

 private:
  // Profiles with achievement data for this title: live tracker entries plus
  // signed-out accounts whose title GPD exists on disk.
  std::vector<std::pair<uint64_t, std::string>> AchievementProfiles() {
    auto* xam = kernel_state_->xam_state();
    std::vector<std::pair<uint64_t, std::string>> result;
    for (const auto& [xuid, account] : *xam->profile_manager()->GetAccounts()) {
      const bool live = !xam->achievement_manager()
                             ->GetTitleAchievements(xuid, title_id_)
                             .empty();
      std::error_code ec = {};
      const bool on_disk = std::filesystem::is_regular_file(
          ProfilePackageDir(content_root_, xuid) / GpdFileName(title_id_), ec);
      if (live || (!ec && on_disk)) {
        result.emplace_back(xuid, account.GetGamertagString());
      }
    }
    return result;
  }

  std::vector<AchRow> LoadAchievements(uint64_t xuid) {
    std::vector<AchRow> rows;
    auto* achievement_manager =
        kernel_state_->xam_state()->achievement_manager();
    auto live = achievement_manager->GetTitleAchievements(xuid, title_id_);
    if (!live.empty()) {
      for (auto& entry : live) {
        AchRow row;
        row.achievement = std::move(entry);
        const auto icon = achievement_manager->GetAchievementIcon(
            xuid, title_id_, row.achievement.achievement_id);
        row.icon.assign(icon.begin(), icon.end());
        rows.push_back(std::move(row));
      }
      return rows;
    }
    // Signed-out fallback: parse the title GPD straight from the profile
    // package dir (no login needed).
    const auto bytes = ReadFileBytes(ProfilePackageDir(content_root_, xuid) /
                                     GpdFileName(title_id_));
    if (bytes.empty()) {
      return rows;
    }
    kernel::xam::GpdInfoTitle gpd(title_id_, bytes);
    if (!gpd.IsValid()) {
      return {};
    }
    for (const uint32_t id : gpd.GetAchievementsIds()) {
      AchRow row;
      row.achievement = kernel::xam::Achievement(gpd.GetAchievementEntry(id));
      row.achievement.achievement_name = gpd.GetAchievementTitle(id);
      row.achievement.unlocked_description = gpd.GetAchievementDescription(id);
      row.achievement.locked_description =
          gpd.GetAchievementUnachievedDescription(id);
      const auto image = gpd.GetImage(row.achievement.image_id);
      row.icon.assign(image.begin(), image.end());
      rows.push_back(std::move(row));
    }
    return rows;
  }

  void BuildAchievementsPage() {
    auto* page = new wxPanel(book_, wxID_ANY);
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    auto* top = new wxBoxSizer(wxHORIZONTAL);
    top->Add(new wxStaticText(page, wxID_ANY, "Profile:"), 0,
             wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    profile_choice_ = new wxChoice(page, wxID_ANY);
    top->Add(profile_choice_, 0, wxRIGHT, FromDIP(16));
    info_choice_ = new wxChoice(page, wxID_ANY);
    info_choice_->Append("Default");
    info_choice_->Append("Unlocked info");
    info_choice_->Append("Locked info");
    info_choice_->SetSelection(0);
    info_choice_->SetToolTip(
        "Default shows unlocked info for unlocked achievements and locked "
        "info for locked ones.");
    top->Add(info_choice_, 0, wxALIGN_CENTER_VERTICAL);
    ach_summary_ = new wxStaticText(page, wxID_ANY, "");
    // Reserve worst-case width up front so the fitted dialog always fits the
    // summary text set later in PopulateAchievements.
    ach_summary_->SetMinSize(
        ach_summary_->GetTextExtent("Unlocked 888/888 (88888/88888 G)"));
    top->AddStretchSpacer();
    top->Add(ach_summary_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    sizer->Add(top, 0, wxEXPAND | wxALL, FromDIP(8));
    list_ = new wxListCtrl(page, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                           wxLC_REPORT | wxLC_SINGLE_SEL);
    list_->InsertColumn(0, "", wxLIST_FORMAT_LEFT, FromDIP(kIconPx + 6));
    list_->InsertColumn(1, "Achievement", wxLIST_FORMAT_LEFT, FromDIP(300));
    list_->InsertColumn(2, "Gamerscore", wxLIST_FORMAT_LEFT, FromDIP(90));
    list_->InsertColumn(3, "Unlocked", wxLIST_FORMAT_LEFT, FromDIP(220));
    images_ = new wxImageList(FromDIP(kIconPx), FromDIP(kIconPx), true);
    images_->Add(QuestionBitmap(FromDIP(kIconPx)));
    images_->Add(CheckBitmap(FromDIP(kIconPx)));
    list_->AssignImageList(images_, wxIMAGE_LIST_SMALL);
    sizer->Add(list_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));
    ach_hint_ =
        new wxStaticText(page, wxID_ANY, "No achievements data.",
                         wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER);
    sizer->Add(ach_hint_, 0, wxALIGN_CENTER | wxBOTTOM, FromDIP(8));
    page->SetSizer(sizer);
    book_->AddPage(page, "Achievements", true);
    profile_choice_->Bind(wxEVT_CHOICE, &WxGameContentDialog::OnProfile, this);
    info_choice_->Bind(wxEVT_CHOICE, &WxGameContentDialog::OnInfoMode, this);
  }

  void RefreshAchievements() {
    profile_xuids_.clear();
    profile_choice_->Clear();
    for (const auto& [xuid, name] : AchievementProfiles()) {
      profile_xuids_.push_back(xuid);
      profile_choice_->Append(WxLabel(name));
    }
    if (!profile_xuids_.empty()) {
      profile_choice_->SetSelection(0);
    }
    PopulateAchievements();
  }
  void PopulateAchievements() {
    list_->DeleteAllItems();
    images_->RemoveAll();
    images_->Add(QuestionBitmap(FromDIP(kIconPx)));
    images_->Add(CheckBitmap(FromDIP(kIconPx)));
    rows_.clear();
    if (profile_choice_->GetSelection() >= 0 &&
        size_t(profile_choice_->GetSelection()) < profile_xuids_.size()) {
      rows_ = LoadAchievements(
          profile_xuids_[size_t(profile_choice_->GetSelection())]);
    }
    const AchInfoMode mode =
        static_cast<AchInfoMode>(info_choice_->GetSelection());
    uint32_t unlocked_count = 0, unlocked_score = 0, total_score = 0;
    for (size_t i = 0; i < rows_.size(); i++) {
      const long row = list_->InsertItem(list_->GetItemCount(), 0);
      // wxListCtrl cells are single-line; join title and description.
      std::string text =
          AchTitle(rows_[i], mode) + " - " + AchDescription(rows_[i], mode);
      list_->SetItem(row, 1, WxLabel(text));
      char score[32];
      std::snprintf(score, sizeof(score), "%u G",
                    rows_[i].achievement.gamerscore);
      list_->SetItem(row, 2, WxLabel(score));
      const bool unlocked = rows_[i].achievement.IsUnlocked();
      list_->SetItem(row, 3,
                     WxLabel(unlocked ? AchUnlockedTime(rows_[i]) : "Locked"));
      if (unlocked) {
        unlocked_count++;
        unlocked_score += rows_[i].achievement.gamerscore;
      }
      total_score += rows_[i].achievement.gamerscore;
      // Index 0 is the "?" tile, 1 is the checkmark.
      int index = 1;
      const bool locked = !unlocked;
      if (!rows_[i].icon.empty()) {
        wxBitmap bitmap = BitmapFromBytes(
            rows_[i].icon, locked && mode != AchInfoMode::kUnlocked,
            FromDIP(kIconPx));
        if (bitmap.IsOk()) {
          index = images_->Add(bitmap);
        } else if (locked) {
          index = 0;
        }
      } else if (locked) {
        index = 0;
      }
      list_->SetItemImage(row, index);
    }
    char summary[96];
    std::snprintf(summary, sizeof(summary), "Unlocked %u/%u (%u/%u G)",
                  unlocked_count, uint32_t(rows_.size()), unlocked_score,
                  total_score);
    ach_summary_->SetLabel(WxLabel(summary));
    ach_hint_->Show(rows_.empty());
    Layout();
  }

  void OnProfile(wxCommandEvent&) { PopulateAchievements(); }
  void OnInfoMode(wxCommandEvent&) { PopulateAchievements(); }

  struct SaveRow {
    std::filesystem::path path;
    std::string name;
    std::string size;
    std::string modified;
    std::vector<uint8_t> icon;
  };

  void BuildSavesPage() {
    auto* page = new wxPanel(book_, wxID_ANY);
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    auto* top = new wxBoxSizer(wxHORIZONTAL);
    top->Add(new wxStaticText(page, wxID_ANY, "Profile:"), 0,
             wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    save_choice_ = new wxChoice(page, wxID_ANY);
    top->Add(save_choice_, 0, wxRIGHT, FromDIP(16));
    sizer->Add(top, 0, wxEXPAND | wxALL, FromDIP(8));
    save_list_ = new wxListCtrl(page, wxID_ANY, wxDefaultPosition,
                                wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    save_list_->InsertColumn(0, "", wxLIST_FORMAT_LEFT, FromDIP(kIconPx + 6));
    save_list_->InsertColumn(1, "Save", wxLIST_FORMAT_LEFT, FromDIP(260));
    save_list_->InsertColumn(2, "Size", wxLIST_FORMAT_LEFT, FromDIP(90));
    save_list_->InsertColumn(3, "Modified", wxLIST_FORMAT_LEFT, FromDIP(140));
    save_images_ = new wxImageList(FromDIP(kIconPx), FromDIP(kIconPx), true);
    save_images_->Add(BlankBitmap(FromDIP(kIconPx)));
    save_list_->AssignImageList(save_images_, wxIMAGE_LIST_SMALL);
    save_list_->Bind(wxEVT_LIST_ITEM_RIGHT_CLICK,
                     &WxGameContentDialog::OnSaveContext, this);
    sizer->Add(save_list_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
               FromDIP(8));
    save_hint_ =
        new wxStaticText(page, wxID_ANY, "No saves, so far.", wxDefaultPosition,
                         wxDefaultSize, wxALIGN_CENTER);
    sizer->Add(save_hint_, 0, wxALIGN_CENTER | wxBOTTOM, FromDIP(8));
    page->SetSizer(sizer);
    book_->AddPage(page, "Saves");
    save_choice_->Bind(wxEVT_CHOICE, &WxGameContentDialog::OnSaveProfile, this);
    RefreshSaves();
  }

  void RefreshSaves() {
    save_xuids_.clear();
    save_choice_->Clear();
    for (const auto& [xuid, account] :
         *kernel_state_->xam_state()->profile_manager()->GetAccounts()) {
      std::error_code ec = {};
      if (std::filesystem::is_directory(SaveDir(content_root_, xuid, title_id_),
                                        ec)) {
        save_xuids_.push_back(xuid);
        save_choice_->Append(WxLabel(account.GetGamertagString()));
      }
    }
    if (!save_xuids_.empty()) {
      save_choice_->SetSelection(0);
    }
    PopulateSaves();
  }

  void PopulateSaves() {
    save_list_->DeleteAllItems();
    save_images_->RemoveAll();
    save_images_->Add(BlankBitmap(FromDIP(kIconPx)));
    save_rows_.clear();
    if (save_choice_->GetSelection() >= 0 &&
        size_t(save_choice_->GetSelection()) < save_xuids_.size()) {
      auto* content_manager = kernel_state_->content_manager();
      std::error_code ec = {};
      for (const auto& entry : std::filesystem::directory_iterator(
               SaveDir(content_root_,
                       save_xuids_[size_t(save_choice_->GetSelection())],
                       title_id_),
               ec)) {
        SaveRow row;
        row.path = entry.path();
        row.name = xe::path_to_utf8(entry.path().filename());
        row.size = "";
        row.modified = "";
        auto package = content_manager->OpenPackage(entry.path());
        if (package && package->IsValidPackage()) {
          const auto metadata = package->GetContentMetadata();
          const std::string display = xe::to_utf8(metadata.display_name());
          if (!display.empty()) {
            row.name = display;
          }
          row.size = FormatBytes(metadata.content_size);
          std::vector<uint8_t> thumbnail;
          if (package->GetThumbnail(thumbnail) == X_STATUS_SUCCESS &&
              !thumbnail.empty()) {
            row.icon = std::move(thumbnail);
          }
        }
        const auto ft = std::filesystem::last_write_time(entry.path(), ec);
        if (!ec) {
          row.modified = FormatFileTime(ft);
        }
        save_rows_.push_back(std::move(row));
      }
    }
    for (size_t i = 0; i < save_rows_.size(); i++) {
      const long row = save_list_->InsertItem(save_list_->GetItemCount(), 0);
      save_list_->SetItem(row, 1, WxLabel(save_rows_[i].name));
      save_list_->SetItem(row, 2, WxLabel(save_rows_[i].size));
      save_list_->SetItem(row, 3, WxLabel(save_rows_[i].modified));
      int index = 0;
      if (!save_rows_[i].icon.empty()) {
        wxBitmap bitmap =
            BitmapFromBytes(save_rows_[i].icon, false, FromDIP(kIconPx));
        if (bitmap.IsOk()) {
          index = save_images_->Add(bitmap);
        }
      }
      save_list_->SetItemImage(row, index);
    }
    save_hint_->Show(save_rows_.empty());
    Layout();
  }

  void OnSaveProfile(wxCommandEvent&) { PopulateSaves(); }

  void OnSaveContext(wxListEvent& event) {
    const long row = event.GetIndex();
    if (row < 0 || size_t(row) >= save_rows_.size()) {
      return;
    }
    wxMenu menu;
    const int id = wxWindow::NewControlId();
    menu.Append(id, "Open containing folder");
    menu.Bind(
        wxEVT_MENU,
        [this, row](wxCommandEvent&) {
          const auto& folder = save_rows_[size_t(row)].path.parent_path();
          wxLaunchDefaultApplication(WxLabel(xe::path_to_utf8(folder)));
        },
        id);
    save_list_->PopupMenu(&menu);
  }

  struct ContentRow {
    std::filesystem::path path;
    std::string name;
    std::string size;
    std::vector<uint8_t> icon;
  };

  struct ContentTab {
    wxListCtrl* list = nullptr;
    wxImageList* images = nullptr;
    std::vector<ContentRow> rows;
  };

  // Shared XUID-0 content (title updates, marketplace): extracted folders
  // read their .header sidecar, package files their inline STFS metadata,
  // both through OpenPackage.
  void BuildContentPage(const char* label, const char* type_dir,
                        ContentTab& tab) {
    auto* page = new wxPanel(book_, wxID_ANY);
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    tab.list = new wxListCtrl(page, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                              wxLC_REPORT | wxLC_SINGLE_SEL);
    tab.list->InsertColumn(0, "", wxLIST_FORMAT_LEFT, FromDIP(kIconPx + 6));
    tab.list->InsertColumn(1, "Name", wxLIST_FORMAT_LEFT, FromDIP(300));
    tab.list->InsertColumn(2, "Size", wxLIST_FORMAT_LEFT, FromDIP(90));
    tab.list->InsertColumn(3, "File", wxLIST_FORMAT_LEFT, FromDIP(200));
    tab.images = new wxImageList(FromDIP(kIconPx), FromDIP(kIconPx), true);
    tab.images->Add(BlankBitmap(FromDIP(kIconPx)));
    tab.list->AssignImageList(tab.images, wxIMAGE_LIST_SMALL);
    tab.list->Bind(wxEVT_LIST_ITEM_RIGHT_CLICK,
                   &WxGameContentDialog::OnContentContext, this);
    sizer->Add(tab.list, 1, wxEXPAND | wxALL, FromDIP(8));
    auto* hint =
        new wxStaticText(page, wxID_ANY, "Nothing installed.",
                         wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER);
    sizer->Add(hint, 0, wxALIGN_CENTER | wxBOTTOM, FromDIP(8));
    page->SetSizer(sizer);
    book_->AddPage(page, WxLabel(label));

    char title_hex[9];
    std::snprintf(title_hex, sizeof(title_hex), "%08X", title_id_);
    const auto dir = content_root_ / "0000000000000000" / title_hex / type_dir;
    auto* content_manager = kernel_state_->content_manager();
    std::error_code ec = {};
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
      ContentRow row;
      row.path = entry.path();
      row.name = xe::path_to_utf8(entry.path().filename());
      row.size = "";
      auto package = content_manager->OpenPackage(entry.path());
      if (package && package->IsValidPackage()) {
        const auto metadata = package->GetContentMetadata();
        const std::string display = xe::to_utf8(metadata.display_name());
        if (!display.empty()) {
          row.name = display;
        }
        row.size = FormatBytes(metadata.content_size);
        std::vector<uint8_t> thumbnail;
        if (package->GetThumbnail(thumbnail) == X_STATUS_SUCCESS &&
            !thumbnail.empty()) {
          row.icon = std::move(thumbnail);
        }
      } else if (entry.is_regular_file(ec)) {
        row.size = FormatBytes(entry.file_size(ec));
      }
      tab.rows.push_back(std::move(row));
    }
    for (size_t i = 0; i < tab.rows.size(); i++) {
      const long row = tab.list->InsertItem(tab.list->GetItemCount(), 0);
      tab.list->SetItem(row, 1, WxLabel(tab.rows[i].name));
      tab.list->SetItem(row, 2, WxLabel(tab.rows[i].size));
      tab.list->SetItem(row, 3,
                        WxLabel(xe::path_to_utf8(tab.rows[i].path.filename())));
      int index = 0;
      if (!tab.rows[i].icon.empty()) {
        wxBitmap bitmap =
            BitmapFromBytes(tab.rows[i].icon, false, FromDIP(kIconPx));
        if (bitmap.IsOk()) {
          index = tab.images->Add(bitmap);
        }
      }
      tab.list->SetItemImage(row, index);
    }
    hint->Show(tab.rows.empty());
  }

  void OnContentContext(wxListEvent& event) {
    ContentTab* tab = event.GetEventObject() == tu_.list ? &tu_ : &dlc_;
    const long row = event.GetIndex();
    if (row < 0 || size_t(row) >= tab->rows.size()) {
      return;
    }
    const size_t index = size_t(row);
    wxMenu menu;
    const int id = wxWindow::NewControlId();
    menu.Append(id, "Open containing folder");
    menu.Bind(
        wxEVT_MENU,
        [tab, index](wxCommandEvent&) {
          const auto& folder = tab->rows[index].path.parent_path();
          wxLaunchDefaultApplication(WxLabel(xe::path_to_utf8(folder)));
        },
        id);
    tab->list->PopupMenu(&menu);
  }

  kernel::KernelState* kernel_state_;
  std::filesystem::path content_root_;
  const uint32_t title_id_;
  wxNotebook* book_ = nullptr;
  wxChoice* profile_choice_ = nullptr;
  wxChoice* info_choice_ = nullptr;
  wxStaticText* ach_summary_ = nullptr;
  wxListCtrl* list_ = nullptr;
  wxImageList* images_ = nullptr;
  wxStaticText* ach_hint_ = nullptr;
  std::vector<uint64_t> profile_xuids_;
  std::vector<AchRow> rows_;
  wxChoice* save_choice_ = nullptr;
  wxListCtrl* save_list_ = nullptr;
  wxImageList* save_images_ = nullptr;
  wxStaticText* save_hint_ = nullptr;
  std::vector<uint64_t> save_xuids_;
  std::vector<SaveRow> save_rows_;
  ContentTab tu_;
  ContentTab dlc_;
};

}  // namespace

void ShowGameContentDialog(wxWindow* parent, kernel::KernelState* kernel_state,
                           const std::filesystem::path& content_root,
                           uint32_t title_id, const std::string& title_name) {
  if (!parent || !kernel_state || !title_id) {
    return;
  }
  WxGameContentDialog dialog(parent, kernel_state, content_root, title_id,
                             title_name);
  dialog.ShowModal();
}

}  // namespace wx_ui
}  // namespace app
}  // namespace xe
