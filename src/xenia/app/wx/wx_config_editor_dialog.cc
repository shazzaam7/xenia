/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// Modal editor for xenia-canary.config.toml built from the cvar registry.
// One treebook page per category (the TOML sections, same order SaveConfig
// writes); dotted categories nest, so GPU.Debug sits under GPU and HID.Key
// under HID. The control for each row is chosen from the variable's type and
// the optional ConfigVarEditorInfo registered next to its definition; that
// choice, the validation and the value formatting live in wx_config_row.h so
// the per-game editor (wx_game_config_dialog.cc) builds identical rows.
// Nothing is applied until Save, which also writes the file and then confirms
// with a message box. The one exception is "Reset to defaults": after a
// confirmation it puts every variable back to its default, applies that live
// and rewrites the file immediately (the confirmation doubles as its report,
// since the pages visibly change to the defaults).
//
// "Simple view" (the default) restricts the tree to the curated list of
// settings the launcher's config UI exposes (see IsSimpleSetting); unchecking
// it shows every non-transient cvar. The search box filters all settings by
// name and description into a single flat results page.
//
// Page contents are built lazily: the treebook nodes all exist up front (the
// sidebar needs their labels), but a page only gets its controls the first
// time it is shown. The dialog opens on one page, so building every page's
// control tree before it appears - ~285 rows in the Advanced view - is pure
// startup cost; see AddDeferredPage / EnsurePageBuilt.

#include "xenia/app/wx/wx_config_editor_dialog.h"
#include "xenia/app/wx/wx_util.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/dirdlg.h>
#include <wx/filedlg.h>
#include <wx/msgdlg.h>
#include <wx/scrolwin.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/spinctrl.h>
#include <wx/srchctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/timer.h>
#include <wx/treebook.h>
#include <wx/wupdlock.h>

#include "xenia/app/wx/wx_config_row.h"
#include "xenia/app/wx/wx_util.h"
#include "xenia/app/wx/wx_window.h"
#include "xenia/app/wx/wx_window_priv.h"
#include "xenia/base/cvar.h"
#include "xenia/config.h"

namespace xe {
namespace app {
namespace wx_ui {
namespace {

class WxConfigEditorDialog : public wxDialog {
 public:
  explicit WxConfigEditorDialog(wxWindow* parent)
      : wxDialog(parent, wxID_ANY, "Config editor", wxDefaultPosition,
                 wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
    if (cvar::ConfigVars) {
      for (const auto& [name, var] : *cvar::ConfigVars) {
        if (!var->is_transient()) {
          vars_.push_back(var);
        }
      }
    }
    std::ranges::sort(vars_, [](auto a, auto b) {
      if (a->category() != b->category()) {
        return a->category() < b->category();
      }
      return a->name() < b->name();
    });

    search_timer_.SetOwner(this);
    book_ = new wxTreebook(this, wxID_ANY);
    BuildPages(nullptr);

    search_ = new wxSearchCtrl(this, wxID_ANY);
    search_->SetHint("Search settings");
    search_->SetDescriptiveText("Search all settings by name or description");
    search_->ShowCancelButton(true);
    search_->SetToolTip("Filter all settings by name or description.");

    simple_check_ = new wxCheckBox(this, wxID_ANY, "Simple view");
    simple_check_->SetValue(true);
    simple_check_->SetToolTip("Show only the commonly used settings.");

    auto* outer = new wxBoxSizer(wxVERTICAL);
    outer->Add(search_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));
    outer->Add(book_, 1, wxEXPAND | wxALL, FromDIP(8));
    save_button_ = new wxButton(this, wxID_SAVE, "Save");
    reset_button_ = new wxButton(this, wxID_ANY, "Reset to defaults");
    reset_button_->SetToolTip(
        "Set every setting back to its default value and overwrite the config "
        "file.");
    // The Simple view filter sits on the far left; the two actions that touch
    // the config file are grouped on the right, with a right margin matching
    // the treebook's so Save isn't flush against the window edge.
    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    buttons->Add(simple_check_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxBOTTOM,
                 8);
    buttons->AddStretchSpacer(1);
    buttons->Add(reset_button_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxBOTTOM,
                 8);
    buttons->Add(save_button_, 0,
                 wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT | wxBOTTOM,
                 FromDIP(8));
    outer->Add(buttons, 0, wxEXPAND | wxBOTTOM, FromDIP(8));
    SetSizer(outer);
    Fit();
    wxSize size = GetSize();
    // Wider than the old notebook cap: the tree sidebar takes ~150px next to
    // the same 600px content column.
    size.x = std::min(size.x, FromDIP(800));
    size.y = std::min(size.y, FromDIP(640));
    SetSize(size);

    save_button_->Bind(wxEVT_BUTTON, &WxConfigEditorDialog::OnSave, this);
    reset_button_->Bind(wxEVT_BUTTON, &WxConfigEditorDialog::OnResetToDefaults,
                        this);
    // Builds a page's controls the first time it is shown (see
    // EnsurePageBuilt).
    book_->Bind(wxEVT_TREEBOOK_PAGE_CHANGED,
                &WxConfigEditorDialog::OnPageChanged, this);
    simple_check_->Bind(wxEVT_CHECKBOX,
                        &WxConfigEditorDialog::OnToggleSimpleView, this);
    search_->Bind(wxEVT_TEXT, &WxConfigEditorDialog::OnSearch, this);
    // Id-filtered so unrelated wxTimer events can't reach the debounce handler.
    Bind(wxEVT_TIMER, &WxConfigEditorDialog::OnSearchTimer, this,
         search_timer_.GetId());
  }

 private:
  // Greys out every control of a row. Used for the internal defaults_date
  // entry, which stays visible so the page layout is honest but cannot be
  // edited.
  static void EnableRowControls(ConfigRow* entry, bool enable) {
    std::vector<wxWindow*> controls = {entry->check,       entry->choice,
                                       entry->slider,      entry->spin,
                                       entry->spin_double, entry->text};
    for (wxCheckBox* check : entry->flag_checks) {
      controls.push_back(check);
    }
    for (wxWindow* control : controls) {
      if (control) {
        control->Enable(enable);
      }
    }
  }

  void AddRow(wxScrolledWindow* scrolled, wxBoxSizer* col,
              cvar::IConfigVar* var,
              const std::map<std::string, std::string>* initial_values,
              bool show_category) {
    const bool internal = var->name() == kInternalConfigVar;
    const cvar::ConfigVarEditorInfo* info = var->editor_info();
    const char* display_name =
        info && info->display_name ? info->display_name : nullptr;
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    auto* label = new wxStaticText(
        scrolled, wxID_ANY, WxLabel(display_name ? display_name : var->name()));
    label->SetMinSize(wxSize(FromDIP(kConfigNamePx), -1));
    // When a friendly name replaces the key, keep the key discoverable - it's
    // what's actually written to the config file. Search results mix sections,
    // so they also name the section.
    std::string tooltip;
    if (show_category) {
      tooltip += "Section: " + var->category() + "\n\n";
    }
    tooltip += var->description();
    if (display_name) {
      tooltip += "\n\n(config key: " + var->name() + ")";
    }
    label->SetToolTip(WxLabel(tooltip));
    row->Add(label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

    // Rebuilds keep unsaved edits by seeding controls from the collected
    // values instead of the config file.
    std::string current = var->display_value();
    if (initial_values) {
      auto it = initial_values->find(var->name());
      if (it != initial_values->end()) {
        current = it->second;
      }
    }

    ConfigRow entry;
    BuildConfigRow(var, scrolled, this, row, &entry, current);
    if (internal) {
      label->Enable(false);
      EnableRowControls(&entry, false);
    } else {
      rows_.push_back(entry);
    }
    col->Add(row, 0, wxEXPAND | wxALL, FromDIP(2));
  }

  // A page whose rows have not been created yet. The page window and its
  // treebook node exist from the moment the tree is built (the sidebar shows
  // the labels), but the controls only appear once the page is displayed.
  struct DeferredPage {
    wxScrolledWindow* scrolled = nullptr;
    wxBoxSizer* col = nullptr;
    std::vector<cvar::IConfigVar*> vars;
  };

  // Creates a page window, adds its treebook node and registers it for lazy
  // population under its page index. The returned entry is stable (it lives in
  // a std::map) so the caller can append the page's variables to it.
  DeferredPage& AddDeferredPage(const std::string& category,
                                std::string& last_top_level,
                                std::vector<size_t>& top_pages) {
    auto* page = new wxPanel(book_, wxID_ANY);
    auto* scrolled = new wxScrolledWindow(page, wxID_ANY, wxDefaultPosition,
                                          wxDefaultSize, wxVSCROLL);
    scrolled->SetScrollRate(0, FromDIP(10));
    // A page with no sizer has no best size of its own, so an unbuilt page
    // would let the book shrink to the first page built. Padding every page to
    // the content cap keeps the dialog's size independent of the build order.
    page->SetMinSize(
        FromDIP(wxSize(kConfigPageContentWidth + 2 * kConfigPageBorder,
                       kConfigPageContentHeight + 2 * kConfigPageBorder)));

    const auto dot = category.find('.');
    const std::string parent =
        dot == std::string::npos ? category : category.substr(0, dot);
    const int index = static_cast<int>(book_->GetPageCount());
    if (parent == last_top_level) {
      book_->AddSubPage(page, WxLabel(category.substr(dot + 1)), false);
    } else {
      last_top_level = parent;
      top_pages.push_back(static_cast<size_t>(index));
      book_->AddPage(page, WxLabel(category), false);
    }

    DeferredPage& entry = deferred_pages_[index];
    entry.scrolled = scrolled;
    entry.col = new wxBoxSizer(wxVERTICAL);
    return entry;
  }

  // Builds one page's rows the first time it is shown. seed_values_ holds the
  // unsaved edits collected when the view or search last changed, which is
  // enough for a page built later: it cannot have been edited in between,
  // because its controls did not exist.
  void EnsurePageBuilt(int index) {
    auto it = deferred_pages_.find(index);
    if (it == deferred_pages_.end()) {
      return;
    }
    DeferredPage& page = it->second;
    wxWindowUpdateLocker freeze(page.scrolled);
    for (auto* var : page.vars) {
      AddRow(page.scrolled, page.col, var, &seed_values_, false);
    }
    page.vars.clear();
    page.vars.shrink_to_fit();
    FinishConfigPage(page.scrolled, page.col);
    deferred_pages_.erase(it);
  }

  // The treebook shows a single page, so that is the only one worth building
  // now; the rest are built by OnPageChanged as they are selected.
  void EnsureVisiblePageBuilt() {
    int selection = book_->GetSelection();
    if (selection == wxNOT_FOUND) {
      book_->SetSelection(0);
      selection = book_->GetSelection();
    }
    EnsurePageBuilt(selection);
  }

  void OnPageChanged(wxBookCtrlEvent& event) {
    EnsurePageBuilt(event.GetSelection());
    event.Skip();
  }

  // (Re)creates the book contents, honouring the current view filter or the
  // search query. initial_values, when given, overrides the file values (used
  // when toggling the view or typing so unsaved edits survive).
  void BuildPages(const std::map<std::string, std::string>* initial_values) {
    // The dialog is already on screen when the view flips or a search is
    // typed, so keep it from repainting once per added control.
    wxWindowUpdateLocker freeze(this);

    book_->DeleteAllPages();
    rows_.clear();
    deferred_pages_.clear();
    seed_values_ =
        initial_values ? *initial_values : std::map<std::string, std::string>();

    if (!search_query_.empty()) {
      BuildSearchPage();
      return;
    }

    std::string last_category;
    // Dotted categories ("GPU.Debug") nest under their parent page, which
    // always sorts first; a dotted category whose parent has no visible rows
    // is promoted to a top-level page under its full name instead.
    std::string last_top_level;
    std::vector<size_t> top_pages;
    DeferredPage* page = nullptr;
    for (auto* var : vars_) {
      if (simple_view_ && !IsSimpleSetting(var->name())) {
        continue;
      }
      if (!page || var->category() != last_category) {
        last_category = var->category();
        page = &AddDeferredPage(last_category, last_top_level, top_pages);
      }
      page->vars.push_back(var);
    }
    for (size_t top : top_pages) {
      book_->ExpandNode(top);
    }
    EnsureVisiblePageBuilt();
  }

  // Single flat page with every setting matching the search query, across all
  // sections and regardless of the Simple view filter.
  void BuildSearchPage() {
    auto* page = new wxPanel(book_, wxID_ANY);
    auto* scrolled = new wxScrolledWindow(page, wxID_ANY, wxDefaultPosition,
                                          wxDefaultSize, wxVSCROLL);
    scrolled->SetScrollRate(0, FromDIP(10));
    auto* col = new wxBoxSizer(wxVERTICAL);
    size_t matches = 0;
    for (auto* var : vars_) {
      if (var->name() == kInternalConfigVar ||
          !MatchesConfigSearch(var, search_query_)) {
        continue;
      }
      ++matches;
      AddRow(scrolled, col, var, &seed_values_, true);
    }
    if (!matches) {
      col->Add(new wxStaticText(scrolled, wxID_ANY,
                                "No settings match this search."),
               0, wxALL, FromDIP(8));
    }
    FinishConfigPage(scrolled, col);
    book_->AddPage(page,
                   WxLabel("Search results (" + std::to_string(matches) + ")"),
                   true);
  }

  std::map<std::string, std::string> CollectValues() const {
    std::map<std::string, std::string> values;
    for (const auto& row : rows_) {
      values.emplace(row.var->name(), ConfigRowValue(row));
    }
    return values;
  }

  void OnToggleSimpleView(wxCommandEvent&) {
    simple_view_ = simple_check_->GetValue();
    // The Simple filter only applies to the tree; entering it clears an
    // active search. Clear() is event-silent, so this rebuilds exactly once.
    search_->Clear();
    search_timer_.Stop();
    search_query_.clear();
    const std::map<std::string, std::string> values = CollectValues();
    BuildPages(&values);
    book_->InvalidateBestSize();
    Layout();
  }

  void OnSearch(wxCommandEvent&) {
    // Debounced: rebuilds happen in OnSearchTimer so fast typing doesn't
    // reshuffle the page under the user on every keystroke.
    search_timer_.StartOnce(200);
  }

  void OnSearchTimer(wxTimerEvent&) {
    const std::string query =
        ToLowerConfigText(TrimConfigText(search_->GetValue().ToStdString()));
    if (query == search_query_) {
      return;
    }
    search_query_ = query;
    const std::map<std::string, std::string> values = CollectValues();
    BuildPages(&values);
    book_->InvalidateBestSize();
    Layout();
  }

  // Whole-config reset. Drops every setting (not just the ones on screen),
  // applies the defaults live and rewrites the config file, unlike Save which
  // only writes the values the built pages still hold.
  void OnResetToDefaults(wxCommandEvent&) {
    const wxString message =
        "Set every setting back to its default value?\n\n"
        "The config file will be overwritten and this cannot be undone.";
    if (wxMessageBox(message, "Config editor", wxOK | wxCANCEL | wxICON_WARNING,
                     this) != wxOK) {
      return;
    }
    for (auto* var : vars_) {
      var->ResetConfigValueToDefault();
    }
    config::SaveConfig();
    // Reseed every page from the defaults just applied - passing no initial
    // values makes each row fall back to the variable's display value, so the
    // edits the rebuilt pages drop are exactly the ones just discarded.
    BuildPages(nullptr);
    book_->InvalidateBestSize();
    Layout();
  }

  void OnSave(wxCommandEvent&) {
    // Validate everything before applying anything. Dropdown and slider values
    // come from fixed sets / ranges, so only free-form text can be invalid.
    std::vector<std::string> invalid;
    std::vector<std::pair<ConfigRow*, std::string>> staged;
    for (auto& row : rows_) {
      if (row.kind != ConfigControlKind::kText) {
        staged.emplace_back(&row, ConfigRowValue(row));
        continue;
      }
      const std::string text =
          TrimConfigText(row.text->GetValue().ToStdString());
      if (!cvar::IsValidConfigValueText(row.var->value_kind(), text)) {
        invalid.push_back(row.var->category() + "." + row.var->name());
        row.text->SetBackgroundColour(ErrorBgColour());
      } else {
        staged.emplace_back(&row, text);
        row.text->SetBackgroundColour(wxNullColour);
      }
      row.text->Refresh();
    }
    if (!invalid.empty()) {
      std::string message = "Invalid values (not saved):\n";
      for (const auto& name : invalid) {
        message += "\n" + name;
      }
      wxMessageBox(WxLabel(message), "Config editor", wxOK | wxICON_ERROR,
                   this);
      return;
    }
    for (const auto& [row, text] : staged) {
      row->var->SetFromString(text);
    }
    config::SaveConfig();
    wxMessageBox("Settings saved.", "Config editor", wxOK | wxICON_INFORMATION,
                 this);
  }

  std::vector<cvar::IConfigVar*> vars_;
  bool simple_view_ = true;
  std::string search_query_;
  wxTreebook* book_ = nullptr;
  std::vector<ConfigRow> rows_;
  // Pages registered by BuildPages but not built yet, keyed by page index.
  std::map<int, DeferredPage> deferred_pages_;
  // Unsaved edits captured by the last BuildPages call; seeds pages that are
  // built after it.
  std::map<std::string, std::string> seed_values_;
  wxSearchCtrl* search_ = nullptr;
  wxCheckBox* simple_check_ = nullptr;
  wxButton* save_button_ = nullptr;
  wxButton* reset_button_ = nullptr;
  wxTimer search_timer_;
};

}  // namespace

void ShowConfigEditorDialog(WxWindow* window) {
  if (!window) {
    return;
  }
  wxWindow* parent = window->view() ? static_cast<wxWindow*>(window->view())
                                    : static_cast<wxWindow*>(window->frame());
  if (!parent) {
    return;
  }
  WxConfigEditorDialog dialog(parent);
  dialog.ShowModal();
}

}  // namespace wx_ui
}  // namespace app
}  // namespace xe
