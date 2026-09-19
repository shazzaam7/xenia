/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// Modal wxWidgets port of kernel::xam::ui::GamercardUI. Widget code only;
// all load/save logic mirrors GamercardUI against the same manager calls.

#include "xenia/app/wx/wx_profile_dialog.h"

#include <wx/bmpbuttn.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dcmemory.h>
#include <wx/dialog.h>
#include <wx/filedlg.h>
#include <wx/image.h>
#include <wx/imaglist.h>
#include <wx/listctrl.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/mstream.h>
#include <wx/scrolwin.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/srchctrl.h>
#include <wx/statbmp.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/utils.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iterator>
#include <map>
#include <set>
#include <vector>

#include "xenia/app/wx/wx_util.h"
#include "xenia/app/wx/wx_window.h"
#include "xenia/app/wx/wx_window_priv.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/png_utils.h"
#include "xenia/base/string.h"
#include "xenia/base/string_util.h"
#include "xenia/emulator.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/title_id_utils.h"
#include "xenia/kernel/xam/profile_manager.h"
#include "xenia/kernel/xam/ui/gamercard_ui.h"
#include "xenia/kernel/xam/user_profile.h"
#include "xenia/kernel/xam/user_settings.h"
#include "xenia/kernel/xam/user_tracker.h"
#include "xenia/kernel/xam/xam_state.h"
#include "xenia/xbox.h"

namespace xe {
namespace app {
namespace wx_ui {

namespace {

constexpr int kIconPx = 64;
constexpr int kLabelPx = 170;

using kernel::xam::ui::GamercardSettings;

wxBitmap BitmapFromBytes(const std::vector<uint8_t>& bytes, int px) {
  if (bytes.empty()) {
    return wxNullBitmap;
  }
  wxMemoryInputStream stream(bytes.data(), bytes.size());
  wxImage image(stream, wxBITMAP_TYPE_ANY);
  if (!image.IsOk()) {
    return wxNullBitmap;
  }
  return wxBitmap(image.Scale(px, px, wxIMAGE_QUALITY_HIGH));
}

wxChoice* AddChoiceRow(wxWindow* parent, wxSizer* column, const char* label,
                       const char* const* items, size_t count, int selection) {
  auto* row = new wxBoxSizer(wxHORIZONTAL);
  auto* text = new wxStaticText(parent, wxID_ANY, WxLabel(label));
  text->SetMinSize(wxSize(kLabelPx, -1));
  row->Add(text, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
  auto* choice = new wxChoice(parent, wxID_ANY);
  for (size_t i = 0; i < count; i++) {
    choice->Append(WxLabel(items[i] ? items[i] : ""));
  }
  choice->SetSelection(std::clamp(selection, 0, int(count) - 1));
  row->Add(choice, 1, wxEXPAND);
  column->Add(row, 0, wxEXPAND | wxTOP, 4);
  return choice;
}

void AddSectionLabel(wxWindow* parent, wxSizer* column, const char* label) {
  auto* text = new wxStaticText(parent, wxID_ANY, WxLabel(label));
  wxFont font = text->GetFont();
  font.MakeBold();
  text->SetFont(font);
  column->Add(text, 0, wxTOP, 8);
}

class WxGamercardDialog : public wxDialog {
 public:
  WxGamercardDialog(wxWindow* parent, kernel::KernelState* kernel_state,
                    uint64_t xuid)
      : wxDialog(parent, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                 wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
        kernel_state_(kernel_state),
        xuid_(xuid) {
    Load();
    SetTitle(WxLabel(std::string(original_.gamertag) + "'s Gamercard"));
    Build();
    Fit();
    wxSize size = GetSize();
    size.x = std::min(size.x, 820);
    size.y = std::min(size.y, 700);
    SetSize(size);
  }

 private:
  void Load() {
    auto* xam = kernel_state_->xam_state();
    auto* profiles = xam->profile_manager();
    const auto* account = profiles->GetAccount(xuid_);
    is_signed_in_ = xam->GetUserProfile(xuid_) != nullptr;

    original_ = GamercardSettings{};
    const std::string gamertag = account->GetGamertagString();
    std::memcpy(original_.gamertag, gamertag.c_str(),
                std::min(gamertag.size(), sizeof(original_.gamertag)));
    original_.country = account->GetCountry();
    original_.language = account->GetLanguage();
    original_.is_live_enabled = account->IsLiveEnabled();
    const std::string online_xuid =
        string_util::to_hex_string(account->GetOnlineXUID());
    std::memcpy(original_.online_xuid, online_xuid.c_str(),
                std::min(online_xuid.size(), sizeof(original_.online_xuid)));
    const auto online_domain = account->GetOnlineDomain();
    std::memcpy(
        original_.online_domain, online_domain.data(),
        std::min(online_domain.size(), sizeof(original_.online_domain)));
    original_.account_subscription_tier = account->GetSubscriptionTier();

    if (is_signed_in_) {
      auto* user = xam->GetUserProfile(xuid_);
      for (const auto setting_id : kernel::xam::ui::UserSettingsToLoad) {
        const auto setting = xam->user_tracker()->GetSetting(
            user, kernel::kDashboardID, static_cast<uint32_t>(setting_id));
        if (setting) {
          original_.gpd_settings[setting_id] = setting->get_host_data();
        }
      }
      const auto tile =
          user->GetProfileIcon(kernel::xam::XTileType::kGamerTile);
      original_.profile_icon.assign(tile.begin(), tile.end());
      auto load_string = [&](kernel::xam::UserSettingId id, char* buffer,
                             size_t size) {
        const auto text =
            xe::to_utf8(std::get<std::u16string>(original_.gpd_settings[id]));
        std::memcpy(buffer, text.c_str(), std::min(text.size(), size));
      };
      using kernel::xam::UserSettingId;
      load_string(UserSettingId::XPROFILE_GAMERCARD_USER_NAME,
                  original_.gamer_name, sizeof(original_.gamer_name));
      load_string(UserSettingId::XPROFILE_GAMERCARD_MOTTO,
                  original_.gamer_motto, sizeof(original_.gamer_motto));
      load_string(UserSettingId::XPROFILE_GAMERCARD_USER_BIO,
                  original_.gamer_bio, sizeof(original_.gamer_bio));
    }
    values_ = original_;
  }

  void Build() {
    auto* outer = new wxBoxSizer(wxVERTICAL);
    auto* scrolled = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition,
                                          wxDefaultSize, wxVSCROLL);
    scrolled->SetScrollRate(0, 10);
    auto* columns = new wxBoxSizer(wxHORIZONTAL);

    // Left: profile + online settings (mirrors the ImGui table's first
    // column).
    auto* left = new wxBoxSizer(wxVERTICAL);
    auto* base = new wxStaticBoxSizer(wxVERTICAL, scrolled, "Profile Settings");
    gamertag_ = new wxTextCtrl(base->GetStaticBox(), wxID_ANY,
                               WxLabel(std::string(values_.gamertag)));
    default_gamertag_bg_ = gamertag_->GetBackgroundColour();
    auto* tag_row = new wxBoxSizer(wxHORIZONTAL);
    auto* tag_label =
        new wxStaticText(base->GetStaticBox(), wxID_ANY, "Gamertag:");
    tag_label->SetMinSize(wxSize(kLabelPx, -1));
    tag_row->Add(tag_label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    tag_row->Add(gamertag_, 1, wxEXPAND);
    base->Add(tag_row, 0, wxEXPAND | wxTOP, 4);
    gamertag_->Bind(wxEVT_TEXT, &WxGamercardDialog::OnGamertag, this);

    icon_button_ =
        new wxBitmapButton(base->GetStaticBox(), wxID_ANY,
                           BitmapFromBytes(values_.profile_icon, kIconPx),
                           wxDefaultPosition, wxSize(kIconPx + 8, kIconPx + 8));
    const bool title_open = kernel_state_->title_id() != 0;
    icon_button_->Enable(is_signed_in_ && !title_open);
    icon_button_->SetToolTip(
        title_open ? "Icon change is disabled when title is running."
                   : "Provide a PNG image with a resolution of 64x64 or "
                     "32x32. Icon will refresh after relog.");
    icon_button_->Bind(wxEVT_BUTTON, &WxGamercardDialog::OnPickIcon, this);
    base->Add(icon_button_, 0, wxTOP, 4);

    auto add_readonly = [&](const char* label, const std::string& value,
                            bool multiline = false) {
      auto* row = new wxBoxSizer(wxHORIZONTAL);
      auto* text =
          new wxStaticText(base->GetStaticBox(), wxID_ANY, WxLabel(label));
      text->SetMinSize(wxSize(kLabelPx, -1));
      row->Add(text, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
      row->Add(new wxTextCtrl(
                   base->GetStaticBox(), wxID_ANY, WxLabel(value),
                   wxDefaultPosition, wxDefaultSize,
                   multiline ? wxTE_MULTILINE | wxTE_READONLY : wxTE_READONLY),
               1, wxEXPAND);
      base->Add(row, 0, wxEXPAND | wxTOP, 4);
    };
    if (is_signed_in_) {
      add_readonly("Gamer Name:", std::string(values_.gamer_name));
      add_readonly("Gamer Motto:", std::string(values_.gamer_motto));
      add_readonly("Gamer Bio:", std::string(values_.gamer_bio),
                   /*multiline=*/true);
    }
    language_ = AddChoiceRow(base->GetStaticBox(), base,
                             "Language:", kernel::xam::ui::XLanguageName,
                             std::size(kernel::xam::ui::XLanguageName),
                             static_cast<int>(values_.language));
    country_ = AddChoiceRow(base->GetStaticBox(), base,
                            "Country:", kernel::xam::ui::XOnlineCountry,
                            std::size(kernel::xam::ui::XOnlineCountry),
                            static_cast<int>(values_.country));
    left->Add(base, 0, wxEXPAND | wxALL, 4);

    auto* online =
        new wxStaticBoxSizer(wxVERTICAL, scrolled, "Online Profile Settings");
    live_ = new wxCheckBox(online->GetStaticBox(), wxID_ANY, "Live Enabled",
                           wxDefaultPosition, wxDefaultSize);
    live_->SetValue(values_.is_live_enabled);
    live_->Bind(wxEVT_CHECKBOX, &WxGamercardDialog::OnLive, this);
    online->Add(live_, 0, wxTOP, 4);
    auto add_online_readonly = [&](const char* label,
                                   const std::string& value) {
      auto* row = new wxBoxSizer(wxHORIZONTAL);
      auto* text =
          new wxStaticText(online->GetStaticBox(), wxID_ANY, WxLabel(label));
      text->SetMinSize(wxSize(kLabelPx, -1));
      row->Add(text, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
      row->Add(new wxTextCtrl(online->GetStaticBox(), wxID_ANY, WxLabel(value),
                              wxDefaultPosition, wxDefaultSize, wxTE_READONLY),
               1, wxEXPAND);
      online->Add(row, 0, wxEXPAND | wxTOP, 4);
    };
    add_online_readonly("Online XUID:", std::string(values_.online_xuid));
    add_online_readonly("Online Domain:", std::string(values_.online_domain));
    zone_ = AddChoiceRow(
        online->GetStaticBox(), online,
        "Gamer Zone:", kernel::xam::ui::XGamerzoneName,
        std::size(kernel::xam::ui::XGamerzoneName),
        GpdInt(kernel::xam::UserSettingId::XPROFILE_GAMERCARD_ZONE));
    zone_present_ = values_.gpd_settings.contains(
        kernel::xam::UserSettingId::XPROFILE_GAMERCARD_ZONE);
    tier_ =
        AddChoiceRow(online->GetStaticBox(), online,
                     "Subscription Tier:", kernel::xam::ui::AccountSubscription,
                     std::size(kernel::xam::ui::AccountSubscription),
                     static_cast<int>(values_.account_subscription_tier));
    left->Add(online, 0, wxEXPAND | wxALL, 4);
    columns->Add(left, 1, wxEXPAND);

    // Right: GPD settings.
    auto* gpd = new wxStaticBoxSizer(wxVERTICAL, scrolled, "Game Settings");
    using kernel::xam::UserSettingId;
    using kernel::xam::ui::GamerDifficultyOptions;
    AddGpdRow(gpd->GetStaticBox(), gpd,
              UserSettingId::XPROFILE_GAMER_DIFFICULTY,
              "Difficulty:", GamerDifficultyOptions,
              std::size(GamerDifficultyOptions));
    AddGpdRow(
        gpd->GetStaticBox(), gpd,
        UserSettingId::XPROFILE_OPTION_CONTROLLER_VIBRATION,
        "Controller Vibration:", kernel::xam::ui::ControllerVibrationOptions,
        std::size(kernel::xam::ui::ControllerVibrationOptions));
    AddGpdRow(
        gpd->GetStaticBox(), gpd,
        UserSettingId::XPROFILE_GAMER_CONTROL_SENSITIVITY,
        "Control Sensitivity:", kernel::xam::ui::ControlSensitivityOptions,
        std::size(kernel::xam::ui::ControlSensitivityOptions));
    AddGpdRow(gpd->GetStaticBox(), gpd,
              UserSettingId::XPROFILE_GAMER_PREFERRED_COLOR_FIRST,
              "Favorite Color (First):", kernel::xam::ui::PreferredColorOptions,
              std::size(kernel::xam::ui::PreferredColorOptions));
    AddGpdRow(
        gpd->GetStaticBox(), gpd,
        UserSettingId::XPROFILE_GAMER_PREFERRED_COLOR_SECOND,
        "Favorite Color (Second):", kernel::xam::ui::PreferredColorOptions,
        std::size(kernel::xam::ui::PreferredColorOptions));
    AddSectionLabel(gpd->GetStaticBox(), gpd, "Action Games Settings");
    AddGpdRow(gpd->GetStaticBox(), gpd,
              UserSettingId::XPROFILE_GAMER_YAXIS_INVERSION,
              "Y-axis Inversion:", kernel::xam::ui::YAxisInversionOptions,
              std::size(kernel::xam::ui::YAxisInversionOptions));
    AddGpdRow(gpd->GetStaticBox(), gpd,
              UserSettingId::XPROFILE_GAMER_ACTION_AUTO_AIM,
              "Auto Aim:", kernel::xam::ui::AutoAimOptions,
              std::size(kernel::xam::ui::AutoAimOptions));
    AddGpdRow(gpd->GetStaticBox(), gpd,
              UserSettingId::XPROFILE_GAMER_ACTION_AUTO_CENTER,
              "Auto Center:", kernel::xam::ui::AutoCenterOptions,
              std::size(kernel::xam::ui::AutoCenterOptions));
    AddGpdRow(gpd->GetStaticBox(), gpd,
              UserSettingId::XPROFILE_GAMER_ACTION_MOVEMENT_CONTROL,
              "Movement Control:", kernel::xam::ui::MovementControlOptions,
              std::size(kernel::xam::ui::MovementControlOptions));
    AddSectionLabel(gpd->GetStaticBox(), gpd, "Racing Games Settings");
    AddGpdRow(gpd->GetStaticBox(), gpd,
              UserSettingId::XPROFILE_GAMER_RACE_TRANSMISSION,
              "Transmission:", kernel::xam::ui::TransmissionOptions,
              std::size(kernel::xam::ui::TransmissionOptions));
    AddGpdRow(gpd->GetStaticBox(), gpd,
              UserSettingId::XPROFILE_GAMER_RACE_CAMERA_LOCATION,
              "Camera Location:", kernel::xam::ui::CameraLocationOptions,
              std::size(kernel::xam::ui::CameraLocationOptions));
    AddGpdRow(gpd->GetStaticBox(), gpd,
              UserSettingId::XPROFILE_GAMER_RACE_BRAKE_CONTROL,
              "Brake Control:", kernel::xam::ui::BrakeControlOptions,
              std::size(kernel::xam::ui::BrakeControlOptions));
    AddGpdRow(
        gpd->GetStaticBox(), gpd,
        UserSettingId::XPROFILE_GAMER_RACE_ACCELERATOR_CONTROL,
        "Accelerator Control:", kernel::xam::ui::AcceleratorControlOptions,
        std::size(kernel::xam::ui::AcceleratorControlOptions));
    AddGpdRow(gpd->GetStaticBox(), gpd, UserSettingId::XPROFILE_GAMER_TYPE,
              "Gamer Type:", kernel::xam::ui::GamerTypeOptions,
              std::size(kernel::xam::ui::GamerTypeOptions));
    columns->Add(gpd, 1, wxEXPAND | wxALL, 4);

    scrolled->SetSizer(columns);
    outer->Add(scrolled, 1, wxEXPAND | wxALL, 8);

    save_button_ = new wxButton(this, wxID_SAVE, "Save");
    auto* cancel_button = new wxButton(this, wxID_CANCEL, "Cancel");
    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    buttons->AddStretchSpacer();
    buttons->Add(save_button_, 0, wxRIGHT, 8);
    buttons->Add(cancel_button, 0, wxRIGHT, 8);
    outer->Add(buttons, 0, wxEXPAND | wxBOTTOM, 8);
    SetSizer(outer);

    save_button_->Bind(wxEVT_BUTTON, &WxGamercardDialog::OnSave, this);
    UpdateLive();
    UpdateGamertag();

    // wxScrolledWindow reports a tiny best size on its own, so size it from
    // the content instead (same as the content install dialog).
    const wxSize content = columns->CalcMin();
    const int width =
        content.GetWidth() + wxSystemSettings::GetMetric(wxSYS_VSCROLL_X) + 32;
    scrolled->SetMinSize(
        wxSize(std::min(width, 820), std::min(content.GetHeight() + 16, 700)));
    Fit();
  }

  int GpdInt(kernel::xam::UserSettingId id) const {
    const auto it = values_.gpd_settings.find(id);
    return it != values_.gpd_settings.end() ? std::get<int32_t>(it->second) : 0;
  }

  void AddGpdRow(wxWindow* parent, wxSizer* column,
                 kernel::xam::UserSettingId id, const char* label,
                 const char* const* items, size_t count) {
    const bool present = values_.gpd_settings.contains(id);
    wxChoice* choice =
        AddChoiceRow(parent, column, label, items, count, GpdInt(id));
    choice->Enable(present);
    gpd_choices_[id] = choice;
  }

  void UpdateLive() {
    const bool live = live_->GetValue();
    zone_->Enable(live && zone_present_);
    tier_->Enable(live);
  }

  void UpdateGamertag() {
    const bool valid = kernel::xam::ProfileManager::IsGamertagValid(
        gamertag_->GetValue().ToStdString());
    gamertag_->SetBackgroundColour(valid ? default_gamertag_bg_
                                         : wxColour(255, 180, 180));
    gamertag_->Refresh();
    save_button_->Enable(valid);
    save_button_->SetToolTip(valid ? ""
                                   : "Saving disabled! Invalid gamertag "
                                     "provided.");
  }

  void OnGamertag(wxCommandEvent&) { UpdateGamertag(); }
  void OnLive(wxCommandEvent&) { UpdateLive(); }

  void OnPickIcon(wxCommandEvent&) {
    wxFileDialog dialog(this, "Select PNG Image", wxString(), wxString(),
                        "PNG Image (*.png)|*.png",
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK) {
      return;
    }
    const auto path = WxToPath(dialog.GetPath());
    if (!IsFilePngImage(path)) {
      return;
    }
    const auto res = GetImageResolution(path);
    if (res != kernel::xam::kProfileIconSizeSmall &&
        res != kernel::xam::kProfileIconSize) {
      return;
    }
    values_.profile_icon = ReadPngFromFile(path);
    icon_button_->SetBitmap(BitmapFromBytes(values_.profile_icon, kIconPx));
  }

  void OnSave(wxCommandEvent&) {
    // Gamertag back into the fixed buffer (zero-padded like the ImGui
    // in-place editor).
    const std::string gamertag = gamertag_->GetValue().ToStdString();
    std::memset(values_.gamertag, 0, sizeof(values_.gamertag));
    std::memcpy(values_.gamertag, gamertag.c_str(),
                std::min(gamertag.size(), sizeof(values_.gamertag)));
    values_.language = static_cast<xe::XLanguage>(language_->GetSelection());
    values_.country = static_cast<xe::XOnlineCountry>(country_->GetSelection());
    values_.is_live_enabled = live_->GetValue();
    if (zone_present_) {
      std::get<int32_t>(
          values_.gpd_settings
              [kernel::xam::UserSettingId::XPROFILE_GAMERCARD_ZONE]) =
          zone_->GetSelection();
    }
    values_.account_subscription_tier =
        static_cast<kernel::xam::X_XAMACCOUNTINFO::AccountSubscriptionTier>(
            tier_->GetSelection());
    for (const auto& [id, choice] : gpd_choices_) {
      if (values_.gpd_settings.contains(id)) {
        std::get<int32_t>(values_.gpd_settings[id]) = choice->GetSelection();
      }
    }
    SaveProfileIcon();
    SaveSettings();
    SaveAccountData();
    EndModal(wxID_SAVE);
  }

  void SaveProfileIcon() {
    if (values_.profile_icon == original_.profile_icon) {
      return;
    }
    kernel_state_->xam_state()->user_tracker()->UpdateUserIcon(
        xuid_, {values_.profile_icon.data(), values_.profile_icon.size()});
  }

  void SaveSettings() {
    auto* tracker = kernel_state_->xam_state()->user_tracker();
    for (const auto& [id, setting] : values_.gpd_settings) {
      if (original_.gpd_settings[id] == setting) {
        continue;
      }
      kernel::xam::UserSetting updated_setting(id, setting);
      tracker->UpsertSetting(xuid_, kernel::kDashboardID, &updated_setting);
    }
    const uint32_t user_index = kernel_state_->xam_state()
                                    ->profile_manager()
                                    ->GetUserIndexAssignedToProfile(xuid_);
    if (user_index != XUserIndexAny) {
      kernel_state_->BroadcastNotification(
          kXNotificationSystemProfileSettingChanged, 0xF);
    }
  }

  void SaveAccountData() {
    auto* profiles = kernel_state_->xam_state()->profile_manager();
    const auto account_original = *profiles->GetAccount(xuid_);
    auto account = account_original;
    account.SetCountry(values_.country);
    account.SetLanguage(values_.language);
    account.SetSubscriptionTier(values_.account_subscription_tier);
    account.ToggleLiveFlag(values_.is_live_enabled);
    const std::u16string gamertag = xe::to_utf16(std::string(values_.gamertag));
    string_util::copy_and_swap_truncating(account.gamertag, gamertag,
                                          std::size(account.gamertag));
    if (std::memcmp(&account, &account_original, sizeof(account)) != 0) {
      if (!is_signed_in_) {
        profiles->MountProfile(xuid_);
      }
      profiles->UpdateAccount(xuid_, &account);
      if (!is_signed_in_) {
        profiles->DismountProfile(xuid_);
      }
    }
  }

  kernel::KernelState* kernel_state_;
  const uint64_t xuid_;
  bool is_signed_in_ = false;
  GamercardSettings original_ = {};
  GamercardSettings values_ = {};

  wxTextCtrl* gamertag_ = nullptr;
  wxColour default_gamertag_bg_;
  wxBitmapButton* icon_button_ = nullptr;
  wxChoice* language_ = nullptr;
  wxChoice* country_ = nullptr;
  wxCheckBox* live_ = nullptr;
  wxChoice* zone_ = nullptr;
  bool zone_present_ = false;
  wxChoice* tier_ = nullptr;
  std::map<kernel::xam::UserSettingId, wxChoice*> gpd_choices_;
  wxButton* save_button_ = nullptr;
};

}  // namespace

bool ShowGamercardDialog(wxWindow* parent, kernel::KernelState* kernel_state,
                         uint64_t xuid) {
  if (!parent || !kernel_state || !xuid) {
    return false;
  }
  WxGamercardDialog dialog(parent, kernel_state, xuid);
  return dialog.ShowModal() == wxID_SAVE;
}

namespace {

// Mirror of CreateProfileUI: gamertag + live validation.
class WxCreateProfileDialog : public wxDialog {
 public:
  WxCreateProfileDialog(wxWindow* parent, kernel::KernelState* kernel_state,
                        bool with_migration)
      : wxDialog(parent, wxID_ANY, "Create Profile", wxDefaultPosition,
                 wxDefaultSize, wxDEFAULT_DIALOG_STYLE),
        kernel_state_(kernel_state),
        with_migration_(with_migration) {
    auto* outer = new wxBoxSizer(wxVERTICAL);
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    auto* label = new wxStaticText(this, wxID_ANY, "Gamertag:");
    label->SetMinSize(wxSize(kLabelPx, -1));
    row->Add(label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    gamertag_ = new wxTextCtrl(this, wxID_ANY);
    gamertag_->SetMaxLength(15);
    gamertag_->SetFocus();
    row->Add(gamertag_, 1, wxEXPAND);
    outer->Add(row, 0, wxEXPAND | wxALL, 8);
    gamertag_->Bind(wxEVT_TEXT, &WxCreateProfileDialog::OnGamertag, this);

    create_button_ = new wxButton(
        this, wxID_OK,
        with_migration ? "Create profile && migrate data" : "Create");
    create_button_->SetDefault();
    auto* cancel_button = new wxButton(this, wxID_CANCEL, "Cancel");
    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    buttons->AddStretchSpacer();
    buttons->Add(create_button_, 0, wxRIGHT, 8);
    buttons->Add(cancel_button, 0, wxRIGHT, 8);
    outer->Add(buttons, 0, wxEXPAND | wxBOTTOM, 8);
    SetSizerAndFit(outer);
    create_button_->Bind(wxEVT_BUTTON, &WxCreateProfileDialog::OnCreate, this);
    UpdateGamertag();
  }

 private:
  void UpdateGamertag() {
    const bool valid = kernel::xam::ProfileManager::IsGamertagValid(
        gamertag_->GetValue().ToStdString());
    gamertag_->SetBackgroundColour(valid ? wxNullColour
                                         : wxColour(255, 180, 180));
    gamertag_->Refresh();
    create_button_->Enable(valid);
  }

  void OnGamertag(wxCommandEvent&) { UpdateGamertag(); }

  void OnCreate(wxCommandEvent&) {
    auto* profiles = kernel_state_->xam_state()->profile_manager();
    const std::string gamertag = gamertag_->GetValue().ToStdString();
    std::set<uint64_t> before;
    for (const auto& [xuid, account] : *profiles->GetAccounts()) {
      before.insert(xuid);
    }
    const bool signed_in_before = profiles->IsAnyProfileSignedIn();
    const bool autologin = profiles->GetAccountCount() == 0;
    if (!profiles->CreateProfile(gamertag, autologin, with_migration_)) {
      wxMessageBox(WxLabel("Failed to create profile \"" + gamertag +
                           "\". Check xenia.log for details."),
                   "Create Profile", wxOK | wxICON_ERROR, this);
      return;
    }
    if (with_migration_) {
      kernel_state_->emulator()->DataMigration(0xB13EBABEBABEBABE);
    }
    // Otherwise the toolbar keeps showing "No profile" and the create looks
    // like it did nothing. When someone is already signed in, leave them.
    if (!signed_in_before) {
      for (const auto& [xuid, account] : *profiles->GetAccounts()) {
        if (!before.contains(xuid)) {
          profiles->Login(xuid);
          break;
        }
      }
    }
    EndModal(wxID_OK);
  }

  kernel::KernelState* kernel_state_;
  const bool with_migration_;
  wxTextCtrl* gamertag_ = nullptr;
  wxButton* create_button_ = nullptr;
};

}  // namespace

bool ShowCreateProfileDialog(wxWindow* parent,
                             kernel::KernelState* kernel_state,
                             bool with_migration) {
  if (!parent || !kernel_state) {
    return false;
  }
  WxCreateProfileDialog dialog(parent, kernel_state, with_migration);
  return dialog.ShowModal() == wxID_OK;
}

namespace {

constexpr int kTitleIconPx = 64;

enum : int {
  kIdTitleSaveDir = wxID_HIGHEST + 300,
  kIdTitleDlcDir,
  kIdTitleUpdateDir,
  kIdTitleRefresh,
  kIdTitleDelete,
};

std::string PlayedLabel(const kernel::xam::TitleInfo& entry) {
  if (!entry.WasTitlePlayed()) {
    return "Unknown";
  }
  const std::time_t t = std::chrono::system_clock::to_time_t(
      std::chrono::system_clock::time_point(
          entry.last_played.time_since_epoch()));
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

class WxPlayedTitlesDialog : public wxDialog {
 public:
  static wxBitmap Placeholder() {
    wxBitmap bitmap(kTitleIconPx, kTitleIconPx, 32);
    wxMemoryDC dc(bitmap);
    dc.SetBackground(
        wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE)));
    dc.Clear();
    dc.SelectObject(wxNullBitmap);
    return bitmap;
  }

  WxPlayedTitlesDialog(wxWindow* parent, kernel::KernelState* kernel_state,
                       uint64_t xuid)
      : wxDialog(parent, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                 wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
        kernel_state_(kernel_state),
        xuid_(xuid) {
    auto* profiles = kernel_state_->xam_state()->profile_manager();
    const auto* profile = profiles->GetProfile(xuid_);
    SetTitle(WxLabel((profile ? profile->name() : "Profile") +
                     std::string("'s Games List")));
    Reload();

    auto* outer = new wxBoxSizer(wxVERTICAL);
    if (info_.size() > 10) {
      search_ = new wxSearchCtrl(this, wxID_ANY);
      search_->SetHint("Search");
      search_->ShowCancelButton(true);
      search_->Bind(wxEVT_SEARCH, &WxPlayedTitlesDialog::OnSearch, this);
      search_->Bind(wxEVT_TEXT, &WxPlayedTitlesDialog::OnSearch, this);
      search_->Bind(wxEVT_SEARCH_CANCEL, &WxPlayedTitlesDialog::OnSearchCancel,
                    this);
      outer->Add(search_, 0, wxEXPAND | wxALL, 8);
    }
    if (info_.empty()) {
      auto* hint =
          new wxStaticText(this, wxID_ANY, "There are no titles, so far.",
                           wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER);
      outer->Add(hint, 1, wxEXPAND | wxALL, 16);
    } else {
      list_ = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                             wxLC_REPORT | wxLC_SINGLE_SEL);
      list_->InsertColumn(0, "", wxLIST_FORMAT_LEFT, kTitleIconPx + 6);
      list_->InsertColumn(1, "Title", wxLIST_FORMAT_LEFT, 220);
      list_->InsertColumn(2, "Achievements", wxLIST_FORMAT_LEFT, 220);
      list_->InsertColumn(3, "Last played", wxLIST_FORMAT_LEFT, 140);
      images_ = new wxImageList(kTitleIconPx, kTitleIconPx, true);
      images_->Add(Placeholder());
      list_->AssignImageList(images_, wxIMAGE_LIST_SMALL);
      list_->Bind(wxEVT_LIST_ITEM_RIGHT_CLICK, &WxPlayedTitlesDialog::OnContext,
                  this);
      outer->Add(list_, 1, wxEXPAND | wxALL, 8);
      Populate();
    }
    auto* close_button = new wxButton(this, wxID_CLOSE, "Close");
    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    buttons->AddStretchSpacer();
    buttons->Add(close_button, 0, wxRIGHT | wxBOTTOM, 8);
    outer->Add(buttons, 0, wxEXPAND);
    SetSizer(outer);
    Fit();
    wxSize size = GetSize();
    size.x = std::min(size.x, 760);
    size.y = std::min(size.y, 600);
    SetSize(size);
    close_button->Bind(
        wxEVT_BUTTON, [this](wxCommandEvent&) { Close(); }, wxID_CLOSE);
  }

 private:
  void Reload() {
    info_ = kernel_state_->xam_state()->user_tracker()->GetPlayedTitles(xuid_);
  }

  bool Matches(const kernel::xam::TitleInfo& entry) const {
    if (filter_.empty()) {
      return true;
    }
    std::string name = xe::to_utf8(entry.title_name);
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return name.find(filter_) != std::string::npos;
  }

  void Populate() {
    list_->DeleteAllItems();
    images_->RemoveAll();
    images_->Add(Placeholder());
    order_.clear();
    for (size_t i = 0; i < info_.size(); i++) {
      if (!Matches(info_[i])) {
        continue;
      }
      order_.push_back(i);
      const long row = list_->InsertItem(list_->GetItemCount(), 0);
      list_->SetItemData(row, i);
      list_->SetItem(row, 1, WxLabel(xe::to_utf8(info_[i].title_name)));
      char stats[96];
      std::snprintf(
          stats, sizeof(stats), "%u/%u Achievements unlocked (%u Gamerscore)",
          info_[i].unlocked_achievements_count, info_[i].achievements_count,
          info_[i].title_earned_gamerscore);
      list_->SetItem(row, 2, WxLabel(stats));
      list_->SetItem(row, 3, WxLabel("Last played: " + PlayedLabel(info_[i])));
    }
    // Icons after rows so image indices line up on rebuild.
    for (size_t r = 0; r < order_.size(); r++) {
      const auto& icon = info_[order_[r]].icon;
      int index = 0;
      if (!icon.empty()) {
        wxMemoryInputStream stream(icon.data(), icon.size());
        wxImage image(stream, wxBITMAP_TYPE_ANY);
        if (image.IsOk()) {
          index = images_->Add(wxBitmap(
              image.Scale(kTitleIconPx, kTitleIconPx, wxIMAGE_QUALITY_HIGH)));
        }
      }
      list_->SetItemImage(long(r), index);
    }
  }

  void OnSearch(wxCommandEvent& event) {
    filter_ = event.GetString().ToStdString();
    std::transform(filter_.begin(), filter_.end(), filter_.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    Populate();
  }

  void OnSearchCancel(wxCommandEvent&) {
    filter_.clear();
    search_->Clear();
    Populate();
  }

  void OnContext(wxListEvent& event) {
    const long row = event.GetIndex();
    if (row < 0 || size_t(row) >= order_.size()) {
      return;
    }
    auto& entry = info_[order_[size_t(row)]];
    auto* profiles = kernel_state_->xam_state()->profile_manager();
    const auto save_path = profiles->GetProfileContentPath(
        xuid_, entry.id, xe::XContentType::kSavedGame);
    const auto dlc_path = profiles->GetProfileContentPath(
        0, entry.id, xe::XContentType::kMarketplaceContent);
    const auto tu_path = profiles->GetProfileContentPath(
        0, entry.id, xe::XContentType::kInstaller);
    std::error_code ec = {};
    wxMenu menu;
    menu.Append(kIdTitleSaveDir, "Open savefile directory")
        ->Enable(std::filesystem::exists(save_path, ec));
    ec.clear();
    menu.Append(kIdTitleDlcDir, "Open DLC directory")
        ->Enable(std::filesystem::exists(dlc_path, ec));
    ec.clear();
    menu.Append(kIdTitleUpdateDir, "Open Title Update directory")
        ->Enable(std::filesystem::exists(tu_path, ec));
    menu.AppendSeparator();
    const bool title_open = kernel_state_->emulator()->is_title_open();
    menu.Append(kIdTitleRefresh, "Refresh title stats")->Enable(!title_open);
    menu.Append(kIdTitleDelete, "Delete title...")->Enable(!title_open);
    menu.Bind(
        wxEVT_MENU,
        [this, &entry, save_path, dlc_path, tu_path](wxCommandEvent& e) {
          switch (e.GetId()) {
            case kIdTitleSaveDir:
              wxLaunchDefaultApplication(WxLabel(xe::path_to_utf8(save_path)));
              break;
            case kIdTitleDlcDir:
              wxLaunchDefaultApplication(WxLabel(xe::path_to_utf8(dlc_path)));
              break;
            case kIdTitleUpdateDir:
              wxLaunchDefaultApplication(WxLabel(xe::path_to_utf8(tu_path)));
              break;
            case kIdTitleRefresh: {
              auto* tracker = kernel_state_->xam_state()->user_tracker();
              tracker->RefreshTitleSummary(xuid_, entry.id);
              if (const auto updated =
                      tracker->GetUserTitleInfo(xuid_, entry.id)) {
                entry = *updated;
              }
              Populate();
              break;
            }
            case kIdTitleDelete: {
              std::string warning;
              if (entry.unlocked_achievements_count != 0) {
                warning =
                    "This will erase all unlocked achievements and potentially "
                    "erase progress for this title. ";
              }
              const int answer = wxMessageBox(
                  WxLabel(warning + "Delete this title from the played list?"),
                  "Delete title", wxYES_NO | wxICON_WARNING, this);
              if (answer == wxYES) {
                kernel_state_->xam_state()
                    ->user_tracker()
                    ->RemoveTitleFromPlayedList(xuid_, entry.id);
                Reload();
                Populate();
              }
              break;
            }
            default:
              break;
          }
        },
        kIdTitleSaveDir, kIdTitleDelete);
    list_->PopupMenu(&menu);
  }

  kernel::KernelState* kernel_state_;
  const uint64_t xuid_;
  std::vector<kernel::xam::TitleInfo> info_;
  std::vector<size_t> order_;
  std::string filter_;
  wxListCtrl* list_ = nullptr;
  wxImageList* images_ = nullptr;
  wxSearchCtrl* search_ = nullptr;
};

}  // namespace

void ShowPlayedTitlesDialog(wxWindow* parent, kernel::KernelState* kernel_state,
                            uint64_t xuid) {
  if (!parent || !kernel_state || !xuid) {
    return;
  }
  WxPlayedTitlesDialog dialog(parent, kernel_state, xuid);
  dialog.ShowModal();
}

bool ShowNoProfileDialog(WxWindow* window, kernel::KernelState* kernel_state,
                         const std::filesystem::path& content_root) {
  if (!window || !kernel_state) {
    return false;
  }
  if (kernel_state->xam_state()->profile_manager()->GetAccountCount()) {
    return false;
  }
  wxWindow* parent = window->view() ? static_cast<wxWindow*>(window->view())
                                    : static_cast<wxWindow*>(window->frame());
  if (!parent) {
    return false;
  }
  const bool migrate = !xe::filesystem::ListDirectories(content_root).empty();
  wxDialog dialog(parent, wxID_ANY, "No Profiles Found", wxDefaultPosition,
                  wxDefaultSize, wxDEFAULT_DIALOG_STYLE);
  auto* outer = new wxBoxSizer(wxVERTICAL);
  outer->Add(new wxStaticText(
                 &dialog, wxID_ANY,
                 "There is no profile available! You will not be able to save "
                 "without one.\n\nWould you like to create one?"),
             0, wxALL, 8);
  auto* create_button = new wxButton(
      &dialog, wxID_ANY,
      migrate ? "Create profile && migrate data" : "Create Profile");
  auto* close_button = new wxButton(&dialog, wxID_CANCEL, "Close");
  auto* buttons = new wxBoxSizer(wxHORIZONTAL);
  buttons->Add(create_button, 0, wxRIGHT, 8);
  buttons->Add(close_button, 0);
  outer->Add(buttons, 0, wxALIGN_RIGHT | wxALL, 8);
  dialog.SetSizerAndFit(outer);
  bool created = false;
  create_button->Bind(
      wxEVT_BUTTON,
      [&](wxCommandEvent&) {
        if (ShowCreateProfileDialog(parent, kernel_state, migrate)) {
          created = true;
          dialog.EndModal(wxID_OK);
        }
      },
      create_button->GetId());
  return dialog.ShowModal() == wxID_OK && created;
}

}  // namespace wx_ui
}  // namespace app
}  // namespace xe
