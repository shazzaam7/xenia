/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// Modal wxWidgets port of ConsoleSettingsDialog. Widget code only; staging,
// save and reset semantics mirror the ImGui dialog.

#include "xenia/app/wx/wx_console_settings_dialog.h"
#include "xenia/app/wx/wx_util.h"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/notebook.h>
#include <wx/scrolwin.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/timer.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "xenia/app/console_settings_tables.h"
#include "xenia/app/wx/wx_util.h"
#include "xenia/app/wx/wx_window.h"
#include "xenia/app/wx/wx_window_priv.h"
#include "xenia/emulator.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xam/profile_manager.h"
#include "xenia/kernel/xam/xam_state.h"
#include "xenia/kernel/xconfig.h"
#include "xenia/xbox.h"

namespace xe {
namespace app {
namespace wx_ui {

namespace {

constexpr int kLabelPx = 170;

// Map-backed combo: selection index follows map order, like ImGui's
// DrawCombobox (preview stays blank when the stored value matches nothing).
template <typename T>
wxChoice* AddMapChoice(wxStaticBoxSizer* box, const char* label,
                       const std::map<T, std::string>& options,
                       xe::be<T>& field) {
  wxWindow* parent = box->GetStaticBox();
  auto* row = new wxBoxSizer(wxHORIZONTAL);
  auto* text = new wxStaticText(parent, wxID_ANY, WxLabel(label));
  text->SetMinSize(wxSize(parent->FromDIP(kLabelPx), -1));
  row->Add(text, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, parent->FromDIP(8));
  auto* choice = new wxChoice(parent, wxID_ANY);
  std::vector<T> keys;
  int selection = wxNOT_FOUND;
  for (const auto& [key, name] : options) {
    if (key == field.get()) {
      selection = int(keys.size());
    }
    keys.push_back(key);
    choice->Append(WxLabel(name));
  }
  choice->SetSelection(selection);
  choice->Bind(
      wxEVT_CHOICE,
      [&field, keys](wxCommandEvent& event) {
        field = keys[size_t(event.GetSelection())];
      },
      choice->GetId());
  row->Add(choice, 1, wxEXPAND);
  box->Add(row, 0, wxEXPAND | wxTOP, parent->FromDIP(4));
  return choice;
}

template <typename T>
wxCheckBox* AddFlagCheck(wxStaticBoxSizer* box, const char* label,
                         xe::be<T>& field, T value) {
  wxWindow* parent = box->GetStaticBox();
  auto* check = new wxCheckBox(parent, wxID_ANY, WxLabel(label),
                               wxDefaultPosition, wxDefaultSize);
  check->SetValue((field.get() & value) != 0);
  check->Bind(
      wxEVT_CHECKBOX,
      [&field, value](wxCommandEvent& event) {
        if (event.IsChecked()) {
          field = T(field.get() | value);
        } else {
          field = T(field.get() & ~value);
        }
      },
      check->GetId());
  box->Add(check, 0, wxTOP, parent->FromDIP(4));
  return check;
}
class WxConsoleSettingsDialog : public wxDialog {
 public:
  WxConsoleSettingsDialog(wxWindow* parent, kernel::KernelState* kernel_state)
      : wxDialog(parent, wxID_ANY, "Console settings", wxDefaultPosition,
                 wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
        kernel_state_(kernel_state),
        xconfig_(kernel_state->xconfig()),
        data_(*xconfig_->GetXConfig()) {
    for (const auto& [xuid, account] :
         *kernel_state_->xam_state()->profile_manager()->GetAccounts()) {
      profiles_[xuid] = account.GetGamertagString();
    }

    confirm_timer_.SetOwner(this);
    book_ = new wxNotebook(this, wxID_ANY);
    BuildUserPage();
    BuildSystemPage();

    auto* outer = new wxBoxSizer(wxVERTICAL);
    outer->Add(book_, 1, wxEXPAND | wxALL, FromDIP(8));
    save_button_ = new wxButton(this, wxID_SAVE, "Save");
    saved_label_ = new wxStaticText(this, wxID_ANY, "Settings Saved!");
    saved_label_->Hide();
    reset_button_ = new wxButton(this, wxID_ANY, "Reset", wxDefaultPosition,
                                 wxSize(FromDIP(55), -1));
    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    buttons->Add(save_button_, 0, wxRIGHT, FromDIP(8));
    buttons->Add(saved_label_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT,
                 FromDIP(8));
    buttons->AddStretchSpacer();
    buttons->Add(reset_button_, 0, wxRIGHT | wxBOTTOM, FromDIP(8));
    outer->Add(buttons, 0, wxEXPAND | wxLEFT | wxBOTTOM, FromDIP(8));
    SetSizer(outer);
    Fit();
    wxSize size = GetSize();
    size.x = std::min(size.x, FromDIP(560));
    size.y = std::min(size.y, FromDIP(640));
    SetSize(size);

    save_button_->Bind(wxEVT_BUTTON, &WxConsoleSettingsDialog::OnSave, this);
    reset_button_->Bind(wxEVT_BUTTON, &WxConsoleSettingsDialog::OnReset, this);
    Bind(wxEVT_TIMER, &WxConsoleSettingsDialog::OnConfirmTimer, this);
    Refresh();
  }

 private:
  static wxStaticBoxSizer* Group(wxWindow* parent, wxSizer* column,
                                 const char* label) {
    auto* box = new wxStaticBoxSizer(wxVERTICAL, parent, WxLabel(label));
    column->Add(box, 0, wxEXPAND | wxALL, parent->FromDIP(4));
    return box;
  }

  static wxBoxSizer* Row(wxStaticBoxSizer* box, const char* label) {
    wxWindow* parent = box->GetStaticBox();
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    auto* text = new wxStaticText(parent, wxID_ANY, WxLabel(label));
    text->SetMinSize(wxSize(parent->FromDIP(kLabelPx), -1));
    row->Add(text, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, parent->FromDIP(8));
    box->Add(row, 0, wxEXPAND | wxTOP, parent->FromDIP(4));
    return row;
  }

  // Sizes a notebook page to its content with a scroll fallback, like the
  // content install dialog.
  static void FinishPage(wxWindow* page, wxScrolledWindow* scrolled,
                         wxSizer* col) {
    scrolled->SetSizer(col);
    const wxSize content = col->CalcMin();
    scrolled->SetMinSize(
        wxSize(std::min(content.GetWidth() +
                            wxSystemSettings::GetMetric(wxSYS_VSCROLL_X) +
                            scrolled->FromDIP(16),
                        scrolled->FromDIP(520)),
               std::min(content.GetHeight() + scrolled->FromDIP(16),
                        scrolled->FromDIP(470))));
    auto* page_sizer = new wxBoxSizer(wxVERTICAL);
    page_sizer->Add(scrolled, 1, wxEXPAND | wxALL, scrolled->FromDIP(4));
    page->SetSizer(page_sizer);
  }

  void BuildUserPage() {
    auto* page = new wxPanel(book_, wxID_ANY);
    auto* scrolled = new wxScrolledWindow(page, wxID_ANY, wxDefaultPosition,
                                          wxDefaultSize, wxVSCROLL);
    scrolled->SetScrollRate(0, FromDIP(10));
    auto* col = new wxBoxSizer(wxVERTICAL);

    auto* time = Group(scrolled, col, "Time");
    auto* tz_row = Row(time, "Timezone:");
    timezone_ = new wxChoice(time->GetStaticBox(), wxID_ANY);
    for (const auto& tz : kernel::kTimezones) {
      timezone_->Append(WxLabel(tz.name));
    }
    timezone_->Bind(wxEVT_CHOICE, &WxConsoleSettingsDialog::OnTimezone, this);
    tz_row->Add(timezone_, 1, wxEXPAND);
    dst_ = AddFlagCheck(time, "Disable Daylight-Saving Time",
                        data_.user.retail_flags,
                        static_cast<uint32_t>(kernel::X_RETAIL_FLAGS::DSTOff));
    clock24_ = AddFlagCheck(
        time, "24H Time", data_.user.retail_flags,
        static_cast<uint32_t>(kernel::X_RETAIL_FLAGS::TwentyFourHourClock));

    auto* locale = Group(scrolled, col, "Locale");
    language_ =
        AddMapChoice(locale, "Language:", kLanguageMap, data_.user.language);
    country_ =
        AddMapChoice(locale, "Country:", kCountryMap, data_.user.country);

    auto* profile = Group(scrolled, col, "Profile");
    default_profile_ = AddMapChoice(profile, "Default Profile:", profiles_,
                                    data_.user.default_profile);
    parental_ = AddFlagCheck(
        profile, "Parental Control", data_.user.parental_control_flags,
        static_cast<uint8_t>(kernel::X_PC_FLAGS::PCEnabled));

    auto* retail = Group(scrolled, col, "Retail Options");
    dash_init_ = AddFlagCheck(
        retail, "Dashboard Initialized", data_.user.retail_flags,
        static_cast<uint32_t>(kernel::X_RETAIL_FLAGS::DashboardInitialized));
    iptv_ = AddFlagCheck(
        retail, "IPTV Initialized", data_.user.retail_flags,
        static_cast<uint32_t>(kernel::X_RETAIL_FLAGS::IPTVEnabled));
    dvr_ = AddFlagCheck(
        retail, "DVR Initialized", data_.user.retail_flags,
        static_cast<uint32_t>(kernel::X_RETAIL_FLAGS::IPTVDVREnabled));
    kinect_ = AddFlagCheck(
        retail, "Kinect Initialized", data_.user.retail_flags,
        static_cast<uint32_t>(kernel::X_RETAIL_FLAGS::KinectInitialized));

    FinishPage(page, scrolled, col);
    book_->AddPage(page, "User", true);
  }

  void BuildSystemPage() {
    auto* page = new wxPanel(book_, wxID_ANY);
    auto* scrolled = new wxScrolledWindow(page, wxID_ANY, wxDefaultPosition,
                                          wxDefaultSize, wxVSCROLL);
    scrolled->SetScrollRate(0, FromDIP(10));
    auto* col = new wxBoxSizer(wxVERTICAL);

    auto* video = Group(scrolled, col, "Video Options");
    region_ =
        AddMapChoice(video, "AV Region:", kAVRegion, data_.secured.av_region);
    auto* res_row = Row(video, "Resolution:");
    resolution_ = new wxChoice(video->GetStaticBox(), wxID_ANY);
    for (const auto& res : kernel::XVGAResolution) {
      resolution_->Append(WxLabel(res.name_));
    }
    resolution_->Bind(wxEVT_CHOICE, &WxConsoleSettingsDialog::OnResolution,
                      this);
    res_row->Add(resolution_, 1, wxEXPAND);
    widescreen_ = new wxCheckBox(video->GetStaticBox(), wxID_ANY, "Widescreen");
    widescreen_->Bind(wxEVT_CHECKBOX, &WxConsoleSettingsDialog::OnWidescreen,
                      this);
    video->Add(widescreen_, 0, wxTOP, FromDIP(4));

    auto* audio = Group(scrolled, col, "Audio Options");
    mono_ =
        AddFlagCheck(audio, "Mono", data_.user.audio_flags,
                     static_cast<uint32_t>(kernel::X_AUDIO_FLAGS::AnalogMono));
    prologic_ = AddFlagCheck(
        audio, "Dolby Pro Logic", data_.user.audio_flags,
        static_cast<uint32_t>(kernel::X_AUDIO_FLAGS::DolbyProLogic));
    dolby_ = AddFlagCheck(
        audio, "Dolby Digital", data_.user.audio_flags,
        static_cast<uint32_t>(kernel::X_AUDIO_FLAGS::DolbyDigital));
    wmapro_ = AddFlagCheck(
        audio, "Dolby Digital WMA PRO", data_.user.audio_flags,
        static_cast<uint32_t>(kernel::X_AUDIO_FLAGS::DolbyDigitalWithWMAPRO));
    lowlat_ =
        AddFlagCheck(audio, "Low Latency (unsupported)", data_.user.audio_flags,
                     static_cast<uint32_t>(kernel::X_AUDIO_FLAGS::LowLatency));
    auto* vol_row = Row(audio, "Audio player volume:");
    volume_ = new wxSlider(audio->GetStaticBox(), wxID_ANY, 70, 0, 100,
                           wxDefaultPosition, wxSize(FromDIP(160), -1));
    volume_->Bind(wxEVT_SLIDER, &WxConsoleSettingsDialog::OnVolume, this);
    vol_row->Add(volume_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    volume_label_ = new wxStaticText(audio->GetStaticBox(), wxID_ANY, "");
    vol_row->Add(volume_label_, 0, wxALIGN_CENTER_VERTICAL);

    auto* net = Group(scrolled, col, "Network");
    auto* mac_row = Row(net, "MAC Address:");
    for (size_t i = 0; i < mac_.size(); i++) {
      mac_[i] =
          AddByteBox(net->GetStaticBox(), mac_row,
                     data_.secured.mac_address.data()[i], i + 1 == mac_.size());
    }
    auto* netid_row = Row(net, "Network ID:");
    for (size_t i = 0; i < netid_.size(); i++) {
      netid_[i] = AddByteBox(net->GetStaticBox(), netid_row,
                             data_.secured.online_network_id.data()[i],
                             i + 1 == netid_.size());
    }

    FinishPage(page, scrolled, col);
    book_->AddPage(page, "System");
  }

  static wxTextCtrl* AddByteBox(wxWindow* parent, wxSizer* row, uint8_t& byte,
                                bool last = false) {
    char buf[3];
    std::snprintf(buf, sizeof(buf), "%02X", byte);
    auto* box =
        new wxTextCtrl(parent, wxID_ANY, WxLabel(buf), wxDefaultPosition,
                       wxSize(parent->FromDIP(32), -1));
    box->SetMaxLength(2);
    box->Bind(
        wxEVT_TEXT,
        [&byte, box](wxCommandEvent&) {
          const std::string text = box->GetValue().ToStdString();
          if (text.size() != 2 ||
              text.find_first_not_of("0123456789abcdefABCDEF") !=
                  std::string::npos) {
            box->SetBackgroundColour(ErrorBgColour());
            box->Refresh();
            return;
          }
          box->SetBackgroundColour(wxNullColour);
          box->Refresh();
          byte = uint8_t(std::stoul(text, nullptr, 16));
        },
        box->GetId());
    row->Add(box, 0, wxALIGN_CENTER_VERTICAL | (last ? 0 : wxRIGHT),
             parent->FromDIP(4));
    return box;
  }

  size_t TimezoneIndex() const {
    const kernel::TimeZone current = {
        data_.user.time_zone_bias, data_.user.tz_std_name,
        data_.user.tz_dlt_name,    data_.user.tz_std_date,
        data_.user.tz_dlt_date,    data_.user.tz_std_bias,
        data_.user.tz_dlt_bias};
    size_t index = 0x19;
    auto it = std::find(kernel::kTimezones.cbegin(), kernel::kTimezones.cend(),
                        current);
    if (it != kernel::kTimezones.cend()) {
      index = size_t(std::distance(kernel::kTimezones.cbegin(), it));
    }
    return index;
  }

  int ResolutionIndex() const {
    const int32_t current = data_.user.av_pack_hdmi_sz.get();
    for (size_t i = 0; i < kernel::XVGAResolution.size(); i++) {
      if (kernel::XVGAResolution[i].to_host() == current) {
        return int(i);
      }
    }
    return wxNOT_FOUND;
  }

  template <typename T>
  static void SelectMapChoice(wxChoice* choice,
                              const std::map<T, std::string>& options,
                              T value) {
    int selection = wxNOT_FOUND;
    int i = 0;
    for (const auto& [key, name] : options) {
      if (key == value) {
        selection = i;
        break;
      }
      i++;
    }
    choice->SetSelection(selection);
  }
  void Refresh() {
    timezone_->SetSelection(int(TimezoneIndex()));
    SelectMapChoice(language_, kLanguageMap, data_.user.language.get());
    SelectMapChoice(country_, kCountryMap, data_.user.country.get());
    SelectMapChoice(default_profile_, profiles_,
                    data_.user.default_profile.get());
    SelectMapChoice(region_, kAVRegion, data_.secured.av_region.get());
    resolution_->SetSelection(ResolutionIndex());
    UpdateWidescreen();
    volume_->SetValue(int(data_.user.music_volume.get() * 100));
    UpdateVolumeLabel();
    RefreshBytes();
    const auto flag = [&](wxCheckBox* box, uint32_t field, uint32_t mask) {
      box->SetValue((field & mask) != 0);
    };
    const uint32_t retail = data_.user.retail_flags.get();
    flag(dst_, retail, static_cast<uint32_t>(kernel::X_RETAIL_FLAGS::DSTOff));
    flag(clock24_, retail,
         static_cast<uint32_t>(kernel::X_RETAIL_FLAGS::TwentyFourHourClock));
    flag(dash_init_, retail,
         static_cast<uint32_t>(kernel::X_RETAIL_FLAGS::DashboardInitialized));
    flag(iptv_, retail,
         static_cast<uint32_t>(kernel::X_RETAIL_FLAGS::IPTVEnabled));
    flag(dvr_, retail,
         static_cast<uint32_t>(kernel::X_RETAIL_FLAGS::IPTVDVREnabled));
    flag(kinect_, retail,
         static_cast<uint32_t>(kernel::X_RETAIL_FLAGS::KinectInitialized));
    flag(parental_, data_.user.parental_control_flags.get(),
         static_cast<uint32_t>(kernel::X_PC_FLAGS::PCEnabled));
    const uint32_t audio = data_.user.audio_flags.get();
    flag(mono_, audio,
         static_cast<uint32_t>(kernel::X_AUDIO_FLAGS::AnalogMono));
    flag(prologic_, audio,
         static_cast<uint32_t>(kernel::X_AUDIO_FLAGS::DolbyProLogic));
    flag(dolby_, audio,
         static_cast<uint32_t>(kernel::X_AUDIO_FLAGS::DolbyDigital));
    flag(wmapro_, audio,
         static_cast<uint32_t>(kernel::X_AUDIO_FLAGS::DolbyDigitalWithWMAPRO));
    flag(lowlat_, audio,
         static_cast<uint32_t>(kernel::X_AUDIO_FLAGS::LowLatency));
    const bool enabled = !kernel_state_->emulator()->is_title_open();
    for (size_t i = 0; i < book_->GetPageCount(); i++) {
      book_->GetPage(i)->Enable(enabled);
    }
    save_button_->Enable(enabled);
    reset_button_->Enable(enabled);
  }

  void RefreshBytes() {
    char buf[3];
    for (size_t i = 0; i < mac_.size(); i++) {
      std::snprintf(buf, sizeof(buf), "%02X",
                    data_.secured.mac_address.data()[i]);
      mac_[i]->ChangeValue(WxLabel(buf));
    }
    for (size_t i = 0; i < netid_.size(); i++) {
      std::snprintf(buf, sizeof(buf), "%02X",
                    data_.secured.online_network_id.data()[i]);
      netid_[i]->ChangeValue(WxLabel(buf));
    }
  }

  void UpdateWidescreen() {
    const kernel::Resolution res(data_.user.av_pack_hdmi_sz.get());
    widescreen_->SetValue(
        (data_.user.video_flags.get() &
         static_cast<uint32_t>(kernel::X_VIDEO_FLAGS::Widescreen)) != 0);
    widescreen_->Enable(!res.is_widescreen());
  }

  void UpdateVolumeLabel() {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%.2f", data_.user.music_volume.get());
    volume_label_->SetLabel(WxLabel(buf));
  }

  void OnTimezone(wxCommandEvent& event) {
    const auto& tz = kernel::kTimezones[size_t(event.GetSelection())];
    data_.user.time_zone_bias = tz.timezone_bias;
    std::memcpy(data_.user.tz_std_name.data(), tz.tz_std_name.data(), 4);
    std::memcpy(data_.user.tz_dlt_name.data(), tz.tz_dlt_name.data(), 4);
    std::memcpy(data_.user.tz_std_date.data(), tz.tz_std_date.data(), 4);
    std::memcpy(data_.user.tz_dlt_date.data(), tz.tz_dlt_date.data(), 4);
    data_.user.tz_std_bias = tz.tz_std_bias;
    data_.user.tz_dlt_bias = tz.tz_dlt_bias;
  }

  void OnResolution(wxCommandEvent& event) {
    const auto& res = kernel::XVGAResolution[size_t(event.GetSelection())];
    data_.user.av_pack_hdmi_sz = int32_t(res.to_host());
    if (res.is_widescreen()) {
      data_.user.video_flags =
          data_.user.video_flags.get() |
          static_cast<uint32_t>(kernel::X_VIDEO_FLAGS::Widescreen);
    } else {
      data_.user.video_flags =
          data_.user.video_flags.get() &
          ~static_cast<uint32_t>(kernel::X_VIDEO_FLAGS::Widescreen);
    }
    UpdateWidescreen();
  }

  void OnWidescreen(wxCommandEvent& event) {
    if (event.IsChecked()) {
      data_.user.video_flags =
          data_.user.video_flags.get() |
          static_cast<uint32_t>(kernel::X_VIDEO_FLAGS::Widescreen);
    } else {
      data_.user.video_flags =
          data_.user.video_flags.get() &
          ~static_cast<uint32_t>(kernel::X_VIDEO_FLAGS::Widescreen);
    }
  }

  void OnVolume(wxCommandEvent&) {
    data_.user.music_volume = float(volume_->GetValue()) / 100.f;
    UpdateVolumeLabel();
  }

  void OnSave(wxCommandEvent&) {
    xconfig_->WriteXConfig(&data_);
    saved_label_->Show();
    confirm_timer_.StartOnce(3000);
    Layout();
  }

  void OnReset(wxCommandEvent&) {
    xconfig_->SetDefaults();
    data_ = *xconfig_->GetXConfig();
    Refresh();
    saved_label_->Show();
    confirm_timer_.StartOnce(3000);
    Layout();
  }

  void OnConfirmTimer(wxTimerEvent&) { saved_label_->Hide(); }

  kernel::KernelState* kernel_state_;
  kernel::XConfig* xconfig_;
  kernel::XConfigData data_ = {};
  std::map<uint64_t, std::string> profiles_;

  wxNotebook* book_ = nullptr;
  wxChoice* timezone_ = nullptr;
  wxCheckBox* dst_ = nullptr;
  wxCheckBox* clock24_ = nullptr;
  wxChoice* language_ = nullptr;
  wxChoice* country_ = nullptr;
  wxChoice* default_profile_ = nullptr;
  wxCheckBox* parental_ = nullptr;
  wxCheckBox* dash_init_ = nullptr;
  wxCheckBox* iptv_ = nullptr;
  wxCheckBox* dvr_ = nullptr;
  wxCheckBox* kinect_ = nullptr;
  wxChoice* region_ = nullptr;
  wxChoice* resolution_ = nullptr;
  wxCheckBox* widescreen_ = nullptr;
  wxCheckBox* mono_ = nullptr;
  wxCheckBox* prologic_ = nullptr;
  wxCheckBox* dolby_ = nullptr;
  wxCheckBox* wmapro_ = nullptr;
  wxCheckBox* lowlat_ = nullptr;
  wxSlider* volume_ = nullptr;
  wxStaticText* volume_label_ = nullptr;
  std::array<wxTextCtrl*, 6> mac_ = {};
  std::array<wxTextCtrl*, 4> netid_ = {};
  wxButton* save_button_ = nullptr;
  wxButton* reset_button_ = nullptr;
  wxStaticText* saved_label_ = nullptr;
  wxTimer confirm_timer_;
};

}  // namespace

void ShowConsoleSettingsDialog(WxWindow* window,
                               kernel::KernelState* kernel_state) {
  if (!window || !kernel_state) {
    return;
  }
  wxWindow* parent = window->view() ? static_cast<wxWindow*>(window->view())
                                    : static_cast<wxWindow*>(window->frame());
  if (!parent) {
    return;
  }
  WxConsoleSettingsDialog dialog(parent, kernel_state);
  dialog.ShowModal();
}

}  // namespace wx_ui
}  // namespace app
}  // namespace xe
