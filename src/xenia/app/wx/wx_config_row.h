/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_WX_CONFIG_ROW_H_
#define XENIA_APP_WX_CONFIG_ROW_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/spinctrl.h>
#include <wx/textctrl.h>

#include "xenia/base/cvar.h"

namespace xe {
namespace app {
namespace wx_ui {

// Row building shared by the global config editor and the per-game one, so
// both pick the same control for a variable and honor the same metadata.
//
// The control comes from the variable's type and the optional
// ConfigVarEditorInfo registered next to its definition:
//   - bool                              -> checkbox
//   - choices registered                -> dropdown (combobox)
//   - flags registered                  -> one checkbox per bit, ORed together
//   - numeric with a slider range       -> slider paired with a spin box
//   - path variable                     -> text field plus a browse button
//   - anything else                     -> text field, validated with
//                                          cvar::IsValidConfigValueText before
//                                          use (the engine's from_string
//                                          degrades garbage to T() instead of
//                                          reporting errors).
// Unannotated variables therefore fall back to the text field, which is what
// makes adding metadata purely additive.

enum class ConfigControlKind { kCheck, kChoice, kFlags, kSlider, kPath, kText };

// One setting's controls. The editor owns the struct; wxWidgets owns the
// controls it points at (they die with the page they were built on).
struct ConfigRow {
  cvar::IConfigVar* var = nullptr;
  ConfigControlKind kind = ConfigControlKind::kText;
  // Metadata backing the choice / flags / slider controls; never null for
  // those kinds.
  const cvar::ConfigVarEditorInfo* info = nullptr;
  wxCheckBox* check = nullptr;
  wxChoice* choice = nullptr;
  // Parallel to the dropdown items; maps the selection back to a raw value.
  std::vector<std::string> choice_values;
  // One entry per bit, kept aligned with flag_checks.
  std::vector<wxCheckBox*> flag_checks;
  std::vector<uint64_t> flag_values;
  // The text the row was built with, plus the value it encodes. An untouched
  // control writes this back verbatim so it can't reformat the setting - a
  // gamma of 2.22222233 must not become 2.22, and a mask of -1 (every bit)
  // must not become the sum of the bits the checkbox list happens to cover.
  std::string seeded;
  uint64_t seeded_flags = 0;
  double seeded_number = 0.0;
  // Multiplier between a double's value and its integer slider position (the
  // inverse of the range step), so the two controls stay in sync.
  double slider_scale = 1.0;
  wxSlider* slider = nullptr;
  wxSpinCtrl* spin = nullptr;
  wxSpinCtrlDouble* spin_double = nullptr;
  wxTextCtrl* text = nullptr;
};

constexpr int kConfigNamePx = 250;

// SaveConfig bookkeeping rather than a user option: the global editor shows it
// greyed out, and the per-game editor ignores it entirely (an override of the
// defaults timestamp would be meaningless).
constexpr char kInternalConfigVar[] = "defaults_date";

// Cap on a page's content area, and the floor every page is padded to so the
// dialog's initial size doesn't depend on which page was built first.
constexpr int kConfigPageContentWidth = 600;
constexpr int kConfigPageContentHeight = 470;
// Border wxALL around the scrolled page content, in FinishConfigPage.
constexpr int kConfigPageBorder = 4;

// Chooses the control for `var` and appends it to `row`, which the caller has
// already filled with the label and any columns specific to its editor.
// `parent` owns the controls (normally the page's scrolled window) and
// `dialog_parent` is the window browse dialogs are modal to. `current` is both
// the value the control is seeded with and the text an untouched control writes
// back.
void BuildConfigRow(cvar::IConfigVar* var, wxWindow* parent,
                    wxWindow* dialog_parent, wxBoxSizer* row, ConfigRow* entry,
                    const std::string& current);

// Value currently held by the row's control(s), as raw config text.
std::string ConfigRowValue(const ConfigRow& row);

// Pushes raw config text into the row's existing controls, for undoing an
// override without rebuilding the page ("Use global"). Values outside a
// dropdown's or slider's known set are clamped/added the same way building the
// row with them would have done.
void SetConfigRowValue(ConfigRow* row, const std::string& text);

std::string TrimConfigText(std::string text);
std::string ToLowerConfigText(std::string text);

// Case-insensitive substring match over the key, display name, section and
// description. The query must already be lowercased and trimmed.
bool MatchesConfigSearch(cvar::IConfigVar* var, const std::string& query);

// Settings shown by the "simple" view, taken from the launcher's
// ConfigUiSettings definitions and intersected with the cvars actually present
// here - entries that this codebase doesn't have were dropped.
bool IsSimpleSetting(std::string_view name);

// Sizes a finished book page and forces the layout wxTreebook skips.
void FinishConfigPage(wxScrolledWindow* scrolled, wxSizer* col);

}  // namespace wx_ui
}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_WX_CONFIG_ROW_H_
