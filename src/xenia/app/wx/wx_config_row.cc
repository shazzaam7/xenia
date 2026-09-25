/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/app/wx/wx_config_row.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <set>

#include <wx/button.h>
#include <wx/dirdlg.h>
#include <wx/filedlg.h>
#include <wx/settings.h>
#include <wx/stattext.h>

#include "xenia/app/wx/wx_util.h"

namespace xe {
namespace app {
namespace wx_ui {
namespace {

// Number of decimal places implied by a slider step, so values coming back from
// the spin box are written without floating-point noise.
int DecimalsForStep(double step) {
  int decimals = 0;
  double scaled = std::fabs(step);
  while (decimals < 9 && std::fabs(scaled - std::round(scaled)) > 1e-9) {
    scaled *= 10.0;
    ++decimals;
  }
  return decimals;
}

std::string FormatDouble(double value, double step) {
  std::string text = fmt::format("{:.{}f}", value, DecimalsForStep(step));
  const auto last = text.find_last_not_of('0');
  if (last == std::string::npos) {
    return "0";
  }
  if (text[last] == '.') {
    text.erase(last);
  } else {
    text.erase(last + 1);
  }
  return text;
}

double ParseNumber(const std::string& text) {
  double value = 0.0;
  if (!text.empty()) {
    std::from_chars(text.data(), text.data() + text.size(), value);
  }
  return value;
}

// Flag values may be written in decimal or as 0x... in the config file, so the
// base is auto-detected. Masks may also be negative (-1 meaning every bit), so
// signed input is accepted and reinterpreted.
uint64_t ParseFlags(std::string_view text) {
  if (text.empty()) {
    return 0;
  }
  bool is_negative = false;
  if (text.front() == '-') {
    is_negative = true;
    text.remove_prefix(1);
  }
  // std::from_chars has no auto-detect base on MSVC (bases 2-36 only; base 0
  // divides by zero in the overflow check), so detect 0x here.
  int base = 10;
  if (text.size() > 2 && text.front() == '0' &&
      (text[1] == 'x' || text[1] == 'X')) {
    base = 16;
    text.remove_prefix(2);
  }
  if (text.empty()) {
    return 0;
  }
  if (is_negative) {
    int64_t signed_value = 0;
    std::from_chars(text.data(), text.data() + text.size(), signed_value, base);
    return static_cast<uint64_t>(signed_value);
  }
  uint64_t value = 0;
  std::from_chars(text.data(), text.data() + text.size(), value, base);
  return value;
}

void AddChoiceControl(
    wxWindow* parent, wxBoxSizer* row, ConfigRow* entry,
    const std::vector<cvar::ConfigVarEditorInfo::Choice>& choices,
    const std::string& current) {
  entry->choice = new wxChoice(parent, wxID_ANY);
  int selection = -1;
  for (const auto& choice : choices) {
    entry->choice->Append(WxLabel(choice.label));
    entry->choice_values.emplace_back(choice.value);
    if (selection < 0 && current == choice.value) {
      selection = int(entry->choice_values.size()) - 1;
    }
  }
  if (selection < 0 && !current.empty()) {
    // Keep a hand-edited value outside the known set so saving the rest of
    // the page doesn't silently rewrite it.
    entry->choice->Append(WxLabel(current + " (custom)"));
    entry->choice_values.push_back(current);
    selection = int(entry->choice_values.size()) - 1;
  }
  entry->choice->SetSelection(selection < 0 ? 0 : selection);
  entry->choice->SetToolTip(WxLabel(entry->var->description()));
  row->Add(entry->choice, 1, wxEXPAND);
}

void AddFlagsControl(
    wxWindow* parent, wxBoxSizer* row, ConfigRow* entry,
    const std::vector<cvar::ConfigVarEditorInfo::Choice>& choices,
    const std::string& current) {
  const uint64_t value = ParseFlags(current);
  entry->seeded_flags = value;
  // A few short boxes fit on one line; a long mask would overflow the row, so
  // it gets one line per flag instead.
  auto* flags_sizer = choices.size() > 4 ? new wxBoxSizer(wxVERTICAL)
                                         : new wxBoxSizer(wxHORIZONTAL);
  const int flag_border =
      choices.size() > 4 ? wxBOTTOM : (wxRIGHT | wxALIGN_CENTER_VERTICAL);
  for (const auto& choice : choices) {
    const uint64_t bit = ParseFlags(choice.value);
    auto* check = new wxCheckBox(parent, wxID_ANY, WxLabel(choice.label));
    check->SetValue(bit != 0 && (value & bit) == bit);
    check->SetToolTip(WxLabel(entry->var->description()));
    entry->flag_checks.push_back(check);
    entry->flag_values.push_back(bit);
    flags_sizer->Add(check, 0, flag_border, parent->FromDIP(8));
  }
  row->Add(flags_sizer, 1, wxEXPAND);
}

// Path variable: free-form text (the file may not exist yet, e.g. a log file)
// with a browse button that opens the file or directory chooser.
void AddPathControl(wxWindow* parent, wxWindow* dialog_parent, wxBoxSizer* row,
                    ConfigRow* entry, const std::string& current) {
  entry->text = new wxTextCtrl(parent, wxID_ANY, WxLabel(current));
  entry->text->SetToolTip(WxLabel(entry->var->description()));
  row->Add(entry->text, 1, wxEXPAND);
  auto* browse = new wxButton(parent, wxID_ANY, "...", wxDefaultPosition,
                              wxSize(parent->FromDIP(30), -1));
  browse->SetToolTip("Browse for a path");
  const bool is_directory = entry->info && entry->info->path_is_directory;
  // Only the controls are captured - the ConfigRow itself moves around.
  wxTextCtrl* text = entry->text;
  browse->Bind(
      wxEVT_BUTTON, [dialog_parent, text, is_directory](wxCommandEvent&) {
        const wxString current_path = text->GetValue();
        if (is_directory) {
          wxDirDialog dialog(dialog_parent, "Select a folder", current_path);
          if (dialog.ShowModal() == wxID_OK) {
            text->SetValue(dialog.GetPath());
          }
        } else {
          wxFileDialog dialog(dialog_parent, "Select a file", wxEmptyString,
                              current_path, "All files (*.*)|*.*", wxFD_OPEN);
          if (dialog.ShowModal() == wxID_OK) {
            text->SetValue(dialog.GetPath());
          }
        }
      });
  row->Add(browse, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, parent->FromDIP(4));
}

void AddRangeControl(wxWindow* parent, wxBoxSizer* row, ConfigRow* entry,
                     const std::string& current) {
  const cvar::ConfigVarEditorInfo& info = *entry->info;
  const bool is_double =
      entry->var->value_kind() == cvar::ConfigVarValueKind::kDouble;
  const double step = info.range_step > 0.0 ? info.range_step : 1.0;
  const double value = ParseNumber(current);
  entry->seeded_number = value;
  // wxSlider only has integer positions, so doubles are scaled by the step
  // (e.g. 0.01 -> hundredths) and converted back for display.
  const double scale = is_double ? 1.0 / step : 1.0;
  entry->slider_scale = scale;
  if (is_double) {
    entry->spin_double = new wxSpinCtrlDouble(parent, wxID_ANY);
    entry->spin_double->SetRange(info.range_min, info.range_max);
    entry->spin_double->SetIncrement(step);
    entry->spin_double->SetDigits(DecimalsForStep(step));
    entry->spin_double->SetValue(value);
  } else {
    entry->spin = new wxSpinCtrl(
        parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
        wxSP_ARROW_KEYS, int(std::lround(info.range_min)),
        int(std::lround(info.range_max)), int(std::lround(value)));
  }
  entry->slider =
      new wxSlider(parent, wxID_ANY, int(std::lround(value * scale)),
                   int(std::lround(info.range_min * scale)),
                   int(std::lround(info.range_max * scale)), wxDefaultPosition,
                   wxDefaultSize, wxSL_HORIZONTAL);

  // Capture the child controls (not the local ConfigRow) - wx owns their
  // lifetime.
  wxSlider* slider = entry->slider;
  if (is_double) {
    wxSpinCtrlDouble* spin = entry->spin_double;
    slider->Bind(wxEVT_SLIDER, [slider, spin, scale](wxCommandEvent&) {
      spin->SetValue(double(slider->GetValue()) / scale);
    });
    spin->Bind(wxEVT_SPINCTRLDOUBLE, [slider, spin, scale](wxSpinDoubleEvent&) {
      slider->SetValue(int(std::lround(spin->GetValue() * scale)));
    });
    // wxEXPAND already stretches the slider vertically in this box sizer;
    // pairing it with an alignment flag trips a wxWidgets consistency assert.
    row->Add(slider, 1, wxEXPAND | wxRIGHT, parent->FromDIP(8));
    row->Add(spin, 0, wxALIGN_CENTER_VERTICAL);
  } else {
    wxSpinCtrl* spin = entry->spin;
    slider->Bind(wxEVT_SLIDER, [slider, spin](wxCommandEvent&) {
      spin->SetValue(slider->GetValue());
    });
    spin->Bind(wxEVT_SPINCTRL, [slider, spin](wxSpinEvent&) {
      slider->SetValue(spin->GetValue());
    });
    // wxEXPAND already stretches the slider vertically in this box sizer;
    // pairing it with an alignment flag trips a wxWidgets consistency assert.
    row->Add(slider, 1, wxEXPAND | wxRIGHT, parent->FromDIP(8));
    row->Add(spin, 0, wxALIGN_CENTER_VERTICAL);
  }
  slider->SetToolTip(WxLabel(entry->var->description()));
}

}  // namespace

std::string TrimConfigText(std::string text) {
  const auto first = text.find_first_not_of(" \t");
  if (first == std::string::npos) {
    return "";
  }
  return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}

std::string ToLowerConfigText(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return char(std::tolower(c)); });
  return text;
}

bool MatchesConfigSearch(cvar::IConfigVar* var, const std::string& query) {
  std::string haystack = var->name();
  haystack += '\n';
  haystack += var->category();
  haystack += '\n';
  haystack += var->description();
  const cvar::ConfigVarEditorInfo* info = var->editor_info();
  if (info && info->display_name) {
    haystack += '\n';
    haystack += info->display_name;
  }
  return ToLowerConfigText(haystack).find(query) != std::string::npos;
}

bool IsSimpleSetting(std::string_view name) {
  static const std::set<std::string_view> kSimple = {
      "allow_plugins",
      "anisotropic_override",
      "apply_patches",
      "apply_title_update",
      "apu",
      "async_shader_compilation",
      "break_on_unimplemented_instructions",
      "cl",
      "clear_memory_page_state",
      "controller_hotkeys",
      "d3d12_allow_variable_refresh_rate_and_tearing",
      "d3d12_queue_priority",
      "disable_context_promotion",
      "disassemble_functions",
      "discord",
      "draw_resolution_scale_x",
      "draw_resolution_scale_y",
      "enable_console",
      "enable_xmp",
      "framerate_limit",
      "fullscreen",
      "gpu",
      "gpu_allow_invalid_fetch_constants",
      "hid",
      "keybind_a",
      "keybind_b",
      "keybind_back",
      "keybind_dpad_down",
      "keybind_dpad_left",
      "keybind_dpad_right",
      "keybind_dpad_up",
      "keybind_guide",
      "keybind_left_shoulder",
      "keybind_left_thumb",
      "keybind_left_thumb_down",
      "keybind_left_thumb_left",
      "keybind_left_thumb_right",
      "keybind_left_thumb_up",
      "keybind_left_trigger",
      "keybind_right_shoulder",
      "keybind_right_thumb",
      "keybind_right_thumb_down",
      "keybind_right_thumb_left",
      "keybind_right_thumb_right",
      "keybind_right_thumb_up",
      "keybind_right_trigger",
      "keybind_start",
      "keybind_x",
      "keybind_y",
      "keyboard_mode",
      "keyboard_user_index",
      "launch_module",
      "left_stick_deadzone_percentage",
      "license_mask",
      "log_level",
      "mount_cache",
      "mount_scratch",
      "mute",
      "notification_sound_path",
      "occlusion_query",
      "postprocess_antialiasing",
      "postprocess_dither",
      "postprocess_ffx_cas_additional_sharpness",
      "postprocess_ffx_fsr_max_upsampling_passes",
      "postprocess_ffx_fsr_sharpness_reduction",
      "postprocess_scaling_and_sharpening",
      "present_letterbox",
      "protect_zero",
      "readback_memexport",
      "readback_resolve",
      "render_target_path_d3d12",
      "render_target_path_vulkan",
      "right_stick_deadzone_percentage",
      "scribble_heap",
      "show_achievement_notification",
      "use_dedicated_xma_thread",
      "use_fuzzy_alpha_epsilon",
      "vibration",
      "vsync",
      "vulkan_allow_present_mode_fifo_relaxed",
      "vulkan_allow_present_mode_immediate",
      "vulkan_allow_present_mode_mailbox",
      "xma_decoder",
  };
  return kSimple.count(name) != 0;
}

void BuildConfigRow(cvar::IConfigVar* var, wxWindow* parent,
                    wxWindow* dialog_parent, wxBoxSizer* row, ConfigRow* entry,
                    const std::string& current) {
  entry->var = var;
  entry->info = var->editor_info();
  entry->seeded = current;

  // Fixed choices when registered, otherwise whatever the variable's runtime
  // provider returns (adapters, devices) - null when neither applies.
  const std::vector<cvar::ConfigVarEditorInfo::Choice>* choices = nullptr;
  if (entry->info) {
    if (!entry->info->choices.empty()) {
      choices = &entry->info->choices;
    } else if (entry->info->dynamic_choices) {
      const auto& dynamic = entry->info->dynamic_choices();
      if (!dynamic.empty()) {
        choices = &dynamic;
      }
    }
  }

  if (var->value_kind() == cvar::ConfigVarValueKind::kBool) {
    entry->kind = ConfigControlKind::kCheck;
    entry->check = new wxCheckBox(parent, wxID_ANY, wxEmptyString);
    entry->check->SetValue(current == "true");
    entry->check->SetToolTip(WxLabel(var->description()));
    row->Add(entry->check, 0, wxALIGN_CENTER_VERTICAL);
  } else if (entry->info && entry->info->is_flags && choices) {
    entry->kind = ConfigControlKind::kFlags;
    AddFlagsControl(parent, row, entry, *choices, current);
  } else if (choices) {
    entry->kind = ConfigControlKind::kChoice;
    AddChoiceControl(parent, row, entry, *choices, current);
  } else if (entry->info && entry->info->has_range) {
    entry->kind = ConfigControlKind::kSlider;
    AddRangeControl(parent, row, entry, current);
  } else if (var->value_kind() == cvar::ConfigVarValueKind::kPath) {
    entry->kind = ConfigControlKind::kPath;
    AddPathControl(parent, dialog_parent, row, entry, current);
  } else {
    entry->kind = ConfigControlKind::kText;
    entry->text = new wxTextCtrl(parent, wxID_ANY, WxLabel(current));
    entry->text->SetToolTip(WxLabel(var->description()));
    row->Add(entry->text, 1, wxEXPAND);
  }
}

std::string ConfigRowValue(const ConfigRow& row) {
  switch (row.kind) {
    case ConfigControlKind::kCheck:
      return row.check->GetValue() ? "true" : "false";
    case ConfigControlKind::kChoice: {
      const int selection = row.choice->GetSelection();
      if (selection < 0 || size_t(selection) >= row.choice_values.size()) {
        return "";
      }
      return row.choice_values[size_t(selection)];
    }
    case ConfigControlKind::kFlags: {
      uint64_t value = 0;
      for (size_t i = 0; i < row.flag_checks.size(); ++i) {
        if (row.flag_checks[i]->GetValue()) {
          value |= row.flag_values[i];
        }
      }
      if (value == row.seeded_flags) {
        return row.seeded;
      }
      // Signed masks commonly use -1 for "every bit", so keep the sign
      // rather than writing a value the type can't hold.
      const auto kind = row.var->value_kind();
      if (kind == cvar::ConfigVarValueKind::kInt32 ||
          kind == cvar::ConfigVarValueKind::kInt64) {
        return std::to_string(static_cast<int64_t>(value));
      }
      return std::to_string(value);
    }
    case ConfigControlKind::kSlider:
      if (row.spin_double) {
        if (row.spin_double->GetValue() == row.seeded_number) {
          return row.seeded;
        }
        return FormatDouble(row.spin_double->GetValue(), row.info->range_step);
      }
      return std::to_string(row.spin->GetValue());
    case ConfigControlKind::kPath:
    case ConfigControlKind::kText:
    default:
      return TrimConfigText(row.text->GetValue().ToStdString());
  }
}

void SetConfigRowValue(ConfigRow* row, const std::string& text) {
  switch (row->kind) {
    case ConfigControlKind::kCheck:
      row->check->SetValue(text == "true");
      break;
    case ConfigControlKind::kChoice: {
      int selection = -1;
      for (size_t i = 0; i < row->choice_values.size(); ++i) {
        if (row->choice_values[i] == text) {
          selection = int(i);
          break;
        }
      }
      if (selection < 0) {
        // Not in the list - keep it, exactly as building the row with this
        // value would have done, instead of snapping to the first entry.
        row->choice->Append(WxLabel(text + " (custom)"));
        row->choice_values.push_back(text);
        selection = int(row->choice_values.size()) - 1;
      }
      row->choice->SetSelection(selection);
      break;
    }
    case ConfigControlKind::kFlags: {
      const uint64_t value = ParseFlags(text);
      for (size_t i = 0; i < row->flag_checks.size(); ++i) {
        row->flag_checks[i]->SetValue(row->flag_values[i] != 0 &&
                                      (value & row->flag_values[i]) ==
                                          row->flag_values[i]);
      }
      break;
    }
    case ConfigControlKind::kSlider: {
      const double value = ParseNumber(text);
      if (row->spin_double) {
        row->spin_double->SetValue(value);
        row->slider->SetValue(int(std::lround(value * row->slider_scale)));
      } else {
        row->spin->SetValue(int(std::lround(value)));
        row->slider->SetValue(int(std::lround(value)));
      }
      break;
    }
    case ConfigControlKind::kPath:
    case ConfigControlKind::kText:
    default:
      row->text->SetValue(WxLabel(text));
      break;
  }
}

void FinishConfigPage(wxScrolledWindow* scrolled, wxSizer* col) {
  scrolled->SetSizer(col);
  const wxSize content = col->CalcMin();
  scrolled->SetMinSize(
      wxSize(std::min(content.GetWidth() +
                          wxSystemSettings::GetMetric(wxSYS_VSCROLL_X) +
                          scrolled->FromDIP(16),
                      scrolled->FromDIP(kConfigPageContentWidth)),
             std::min(content.GetHeight() + scrolled->FromDIP(16),
                      scrolled->FromDIP(kConfigPageContentHeight))));
  auto* page = scrolled->GetParent();
  auto* page_sizer = new wxBoxSizer(wxVERTICAL);
  page_sizer->Add(scrolled, 1, wxEXPAND | wxALL,
                  scrolled->FromDIP(kConfigPageBorder));
  page->SetSizer(page_sizer);
  // wxTreebook sizes a page before its sizer exists, and it only re-lays out
  // pages it gains on a size event - which never arrives when the pages are
  // replaced while the book keeps its size (view toggle, search typing).
  // Without this the rebuilt page contents stay collapsed in the corner
  // until the dialog is reopened.
  page->Layout();
  scrolled->Layout();
  scrolled->FitInside();
}

}  // namespace wx_ui
}  // namespace app
}  // namespace xe
