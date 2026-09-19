/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_UI_GAMERCARD_UI_H_
#define XENIA_KERNEL_XAM_UI_GAMERCARD_UI_H_

#include "xenia/kernel/xam/xam_ui.h"

#include <array>

namespace xe {
namespace kernel {
namespace xam {
namespace ui {

// ENUM->NAME tables shared with the wxWidgets port (wx_profile_dialog.cc).
// nullptr entries are intentional gaps matching the underlying enum values.
inline constexpr const char* XLanguageName[] = {nullptr,
                                                "English",
                                                "Japanese",
                                                "German",
                                                "French",
                                                "Spanish",
                                                "Italian",
                                                "Korean",
                                                "Traditional Chinese",
                                                "Portuguese",
                                                "Simplified Chinese",
                                                "Polish",
                                                "Russian"};

inline constexpr const char* XOnlineCountry[] = {nullptr,
                                                 "United Arab Emirates",
                                                 "Albania",
                                                 "Armenia",
                                                 "Argentina",
                                                 "Austria",
                                                 "Australia",
                                                 "Azerbaijan",
                                                 "Belgium",
                                                 "Bulgaria",
                                                 "Bahrain",
                                                 "Brunei Darussalam",
                                                 "Bolivia",
                                                 "Brazil",
                                                 "Belarus",
                                                 "Belize",
                                                 "Canada",
                                                 nullptr,
                                                 "Switzerland",
                                                 "Chile",
                                                 "China",
                                                 "Colombia",
                                                 "Costa Rica",
                                                 "Czech Republic",
                                                 "Germany",
                                                 "Denmark",
                                                 "Dominican Republic",
                                                 "Algeria",
                                                 "Ecuador",
                                                 "Estonia",
                                                 "Egypt",
                                                 "Spain",
                                                 "Finland",
                                                 "Faroe Islands",
                                                 "France",
                                                 "Great Britain",
                                                 "Georgia",
                                                 "Greece",
                                                 "Guatemala",
                                                 "Hong Kong",
                                                 "Honduras",
                                                 "Croatia",
                                                 "Hungary",
                                                 "Indonesia",
                                                 "Ireland",
                                                 "Israel",
                                                 "India",
                                                 "Iraq",
                                                 "Iran",
                                                 "Iceland",
                                                 "Italy",
                                                 "Jamaica",
                                                 "Jordan",
                                                 "Japan",
                                                 "Kenya",
                                                 "Kyrgyzstan",
                                                 "Korea",
                                                 "Kuwait",
                                                 "Kazakhstan",
                                                 "Lebanon",
                                                 "Liechtenstein",
                                                 "Lithuania",
                                                 "Luxembourg",
                                                 "Latvia",
                                                 "Libya",
                                                 "Morocco",
                                                 "Monaco",
                                                 "Macedonia",
                                                 "Mongolia",
                                                 "Macau",
                                                 "Maldives",
                                                 "Mexico",
                                                 "Malaysia",
                                                 "Nicaragua",
                                                 "Netherlands",
                                                 "Norway",
                                                 "New Zealand",
                                                 "Oman",
                                                 "Panama",
                                                 "Peru",
                                                 "Philippines",
                                                 "Pakistan",
                                                 "Poland",
                                                 "Puerto Rico",
                                                 "Portugal",
                                                 "Paraguay",
                                                 "Qatar",
                                                 "Romania",
                                                 "Russian Federation",
                                                 "Saudi Arabia",
                                                 "Sweden",
                                                 "Singapore",
                                                 "Slovenia",
                                                 "Slovak Republic",
                                                 nullptr,
                                                 "El Salvador",
                                                 "Syria",
                                                 "Thailand",
                                                 "Tunisia",
                                                 "Turkey",
                                                 "Trinidad And Tobago",
                                                 "Taiwan",
                                                 "Ukraine",
                                                 "United States",
                                                 "Uruguay",
                                                 "Uzbekistan",
                                                 "Venezuela",
                                                 "Viet Nam",
                                                 "Yemen",
                                                 "South Africa",
                                                 "Zimbabwe"};

inline constexpr const char* AccountSubscription[] = {
    "None",  nullptr, nullptr, "Silver", nullptr,
    nullptr, "Gold",  nullptr, nullptr,  "Family"};

inline constexpr const char* XGamerzoneName[] = {"None", "Recreation", "Pro",
                                                 "Family", "Underground"};

inline constexpr const char* PreferredColorOptions[] = {
    "None", "Black",  "White", "Yellow", "Orange", "Pink",
    "Red",  "Purple", "Blue",  "Green",  "Brown",  "Silver"};

inline constexpr const char* ControllerVibrationOptions[] = {"Off", nullptr,
                                                             nullptr, "On"};

inline constexpr const char* ControlSensitivityOptions[] = {"Medium", "Low",
                                                            "High"};

inline constexpr const char* GamerDifficultyOptions[] = {"Normal", "Easy",
                                                         "Hard"};

inline constexpr const char* AutoAimOptions[] = {"Off", "On"};
inline constexpr const char* AutoCenterOptions[] = {"Off", "On"};
inline constexpr const char* MovementControlOptions[] = {"Left Thumbstick",
                                                         "Right Thumbstick"};
inline constexpr const char* YAxisInversionOptions[] = {"Off", "On"};
inline constexpr const char* TransmissionOptions[] = {"Automatic", "Manual"};
inline constexpr const char* CameraLocationOptions[] = {"Behind", "In Front",
                                                        "Inside"};
inline constexpr const char* BrakeControlOptions[] = {"Trigger", "Button"};
inline constexpr const char* AcceleratorControlOptions[] = {"Trigger",
                                                            "Button"};
inline constexpr const char* GamerTypeOptions[] = {"None",
                                                   nullptr,
                                                   nullptr,
                                                   "Xbox 360 Launch Team",
                                                   "NXE Launch Team",
                                                   "360 + NXE Launch Team"};

inline constexpr std::array<UserSettingId, 19> UserSettingsToLoad = {
    UserSettingId::XPROFILE_GAMER_TYPE,
    UserSettingId::XPROFILE_GAMER_YAXIS_INVERSION,
    UserSettingId::XPROFILE_OPTION_CONTROLLER_VIBRATION,
    UserSettingId::XPROFILE_GAMERCARD_ZONE,
    UserSettingId::XPROFILE_GAMERCARD_REGION,
    UserSettingId::XPROFILE_GAMER_DIFFICULTY,
    UserSettingId::XPROFILE_GAMER_CONTROL_SENSITIVITY,
    UserSettingId::XPROFILE_GAMER_PREFERRED_COLOR_FIRST,
    UserSettingId::XPROFILE_GAMER_PREFERRED_COLOR_SECOND,
    UserSettingId::XPROFILE_GAMER_ACTION_AUTO_AIM,
    UserSettingId::XPROFILE_GAMER_ACTION_AUTO_CENTER,
    UserSettingId::XPROFILE_GAMER_ACTION_MOVEMENT_CONTROL,
    UserSettingId::XPROFILE_GAMER_RACE_TRANSMISSION,
    UserSettingId::XPROFILE_GAMER_RACE_CAMERA_LOCATION,
    UserSettingId::XPROFILE_GAMER_RACE_BRAKE_CONTROL,
    UserSettingId::XPROFILE_GAMER_RACE_ACCELERATOR_CONTROL,
    UserSettingId::XPROFILE_GAMERCARD_USER_NAME,
    UserSettingId::XPROFILE_GAMERCARD_USER_BIO,
    UserSettingId::XPROFILE_GAMERCARD_MOTTO};

struct GamercardSettings {
  // Account settings
  char gamertag[16];
  xe::XOnlineCountry country;
  xe::XLanguage language;
  bool is_live_enabled;
  char online_xuid[0x11];
  char online_domain[0x14];
  X_XAMACCOUNTINFO::AccountSubscriptionTier account_subscription_tier;

  // GPD settings
  std::map<UserSettingId, UserDataTypes> gpd_settings;

  // GPD string buffers
  char gamer_name[0x104];
  char gamer_motto[0x2C];
  char gamer_bio[kMaxUserDataSize];

  // Other
  std::vector<uint8_t> profile_icon;
  // Immediate Texture?
  xe::ui::ImmediateTexture* icon_texture;
};

class GamercardUI final : public XamDialog {
 public:
  GamercardUI(xe::ui::Window* window, xe::ui::ImGuiDrawer* imgui_drawer,
              KernelState* kernel_state, uint64_t xuid);

  ~GamercardUI() = default;

 private:
  void OnDraw(ImGuiIO& io) override;
  void DrawBaseSettings(ImGuiIO& io);
  void DrawOnlineSettings(ImGuiIO& io);
  void DrawGpdSettings(ImGuiIO& io);

  void LoadGamercardInfo();
  void LoadSetting(UserSettingId setting_id);
  void LoadStringSetting(UserSettingId setting_id, char* buffer);
  void SaveSettings();
  void SaveAccountData();
  void SaveProfileIcon();

  void DrawSettingComboBox(UserSettingId setting_id, std::string label,
                           const char* const items[], int item_count,
                           float alignment);

  void DrawInputTextBox(
      std::string label, char* buffer, size_t buffer_size, float alignment,
      std::function<bool(std::span<char>)> on_input_change = {});

  void SelectNewIcon();

  const uint64_t xuid_ = 0;
  const bool is_signed_in_ = false;
  const bool is_valid_gamertag_ = true;

  bool has_opened_ = false;
  KernelState* kernel_state_;
  xe::ui::Window* window_;

  // We're storing OG and current values to compare at the end and send what was
  // changed.
  GamercardSettings gamercardOriginalValues_ = {};
  GamercardSettings gamercardValues_ = {};
};

}  // namespace ui
}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif
