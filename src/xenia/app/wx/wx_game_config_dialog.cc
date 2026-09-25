/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// Per-title config editor, opened from the game library's context menu.
//
// It edits the sparse override file config/<TITLEID>.config.toml that
// config::LoadGameConfig applies over the global config when a title starts.
// Every row pairs the control matching the variable's type and metadata
// (shared with the global editor through wx_config_row.h) with a "Use global"
// button whose tooltip shows the inherited value; clicking it restores that
// value into the control.
//
// A row left equal to the global value is not an override, so it is dropped
// from the file (which also happens through the per-row "Use global" button);
// clearing the last override deletes the file. Opening the dialog first loads
// this title's file into the cvars' per-game layer, so what is on screen and
// what gets written always describe the same title rather than whatever was
// launched last, and closing it drops that layer again - an override is only
// meant to apply to its own title while it runs, so nothing here ever writes
// the global layer (that is the global editor's job). An override the variable
// can no longer read - the setting changed type between builds - is dropped
// when the title's file is loaded, and pruned from the file there.
//
// Pages register up front but build their rows the first time they are shown
// (see EnsurePageBuilt), so opening the dialog and flipping the Simple view
// stay fast no matter how many settings exist; search results are the only
// page built in full.

#include "xenia/app/wx/wx_game_config_dialog.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/dialog.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/settings.h>
#include <wx/sizer.h>
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

std::string ToLower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return char(std::tolower(c)); });
  return text;
}

// Case-insensitive substring match over the key, display name, section and
// description. The query is stored already lowercased.
bool MatchesSearch(cvar::IConfigVar* var, const std::string& query) {
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
  return ToLower(haystack).find(query) != std::string::npos;
}

// Friendly text for a raw value: the dropdown label when the variable has
// fixed or dynamic choices covering it, otherwise the raw text itself.
std::string ChoiceLabelForValue(cvar::IConfigVar* var,
                                const std::string& value) {
  const cvar::ConfigVarEditorInfo* info = var->editor_info();
  if (info) {
    const std::vector<cvar::ConfigVarEditorInfo::Choice>* choices = nullptr;
    if (!info->choices.empty()) {
      choices = &info->choices;
    } else if (info->dynamic_choices) {
      const auto& dynamic = info->dynamic_choices();
      if (!dynamic.empty()) {
        choices = &dynamic;
      }
    }
    if (choices) {
      for (const auto& choice : *choices) {
        if (value == choice.value) {
          return choice.label;
        }
      }
    }
  }
  return value;
}

class WxGameConfigDialog : public wxDialog {
 public:
  WxGameConfigDialog(wxWindow* parent, std::string title_id,
                     std::string title_name)
      : wxDialog(parent, wxID_ANY,
                 WxLabel("Game config - " + title_name + " (" + title_id + ")"),
                 wxDefaultPosition, wxDefaultSize,
                 wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
        title_id_(std::move(title_id)),
        title_name_(std::move(title_name)) {
    if (cvar::ConfigVars) {
      for (const auto& [name, var] : *cvar::ConfigVars) {
        if (!var->is_transient() && var->name() != kInternalConfigVar) {
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

    // Point the per-game layer at this title before anything reads it: it
    // still holds the previous title's overrides, and those would otherwise be
    // shown as this title's and written into its file.
    config::LoadGameConfig(title_id_);

    auto* outer = new wxBoxSizer(wxVERTICAL);

    auto* header = new wxStaticText(
        this, wxID_ANY,
        WxLabel("Overrides for " + title_name_ + "\n" +
                xe::path_to_utf8(config::GameConfigPath(title_id_))));
    header->SetToolTip(
        "Settings that differ from the global config are listed here and "
        "applied whenever this title starts.");
    outer->Add(header, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(8));

    simple_check_ = new wxCheckBox(this, wxID_ANY, "Simple view");
    simple_check_->SetValue(true);
    simple_check_->SetToolTip("Show only the commonly used settings.");

    search_ = new wxSearchCtrl(this, wxID_ANY);
    search_->SetHint("Search settings");
    search_->SetDescriptiveText("Search all settings by name or description");
    search_->ShowCancelButton(true);
    search_->SetToolTip("Filter all settings by name or description.");
    outer->Add(search_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));

    search_timer_.SetOwner(this);
    book_ = new wxTreebook(this, wxID_ANY);
    BuildPages(nullptr);
    outer->Add(book_, 1, wxEXPAND | wxALL, FromDIP(8));

    clear_button_ = new wxButton(this, wxID_ANY, "Clear overrides");
    clear_button_->SetToolTip(
        "Delete every override for this title so it uses the global config.");
    save_button_ = new wxButton(this, wxID_SAVE, "Save");
    close_button_ = new wxButton(this, wxID_CANCEL, "Close");
    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    buttons->Add(simple_check_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxBOTTOM,
                 8);
    buttons->AddStretchSpacer(1);
    buttons->Add(clear_button_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxBOTTOM,
                 8);
    buttons->Add(save_button_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxBOTTOM,
                 8);
    buttons->Add(close_button_, 0,
                 wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT | wxBOTTOM,
                 FromDIP(8));
    outer->Add(buttons, 0, wxEXPAND | wxBOTTOM, FromDIP(8));

    SetSizer(outer);
    Fit();
    wxSize size = GetSize();
    size.x = std::min(size.x, FromDIP(1000));
    size.y = std::min(size.y, FromDIP(700));
    SetSize(size);

    simple_check_->Bind(wxEVT_CHECKBOX, &WxGameConfigDialog::OnToggleSimpleView,
                        this);
    search_->Bind(wxEVT_TEXT, &WxGameConfigDialog::OnSearch, this);
    // Builds a page's controls the first time it is shown (see
    // EnsurePageBuilt).
    book_->Bind(wxEVT_TREEBOOK_PAGE_CHANGED, &WxGameConfigDialog::OnPageChanged,
                this);
    Bind(wxEVT_TIMER, &WxGameConfigDialog::OnSearchTimer, this,
         search_timer_.GetId());
    clear_button_->Bind(wxEVT_BUTTON, &WxGameConfigDialog::OnClearOverrides,
                        this);
    save_button_->Bind(wxEVT_BUTTON, &WxGameConfigDialog::OnSave, this);
    // wxID_CANCEL is handled by wxDialog itself (also for Esc and the frame
    // close button), which ends the modal loop.
  }

 private:
  // One row: the shared control set plus what this editor adds on top of it.
  // Held by unique_ptr so the "Use global" button can bind to a stable address
  // (rows_ reallocates as rows are appended).
  struct GameRow {
    ConfigRow config;
    // What the row falls back to; also what "Use global" restores.
    std::string global_text;
  };

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
      AddRow(page.scrolled, page.col, var, &seed_values_);
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

  void BuildPages(const std::map<std::string, std::string>* initial_values) {
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
      if (!MatchesSearch(var, search_query_)) {
        continue;
      }
      ++matches;
      AddRow(scrolled, col, var, &seed_values_);
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
    for (const auto& entry : rows_) {
      values.emplace(entry->config.var->name(), ConfigRowValue(entry->config));
    }
    return values;
  }

  void AddRow(wxScrolledWindow* scrolled, wxBoxSizer* col,
              cvar::IConfigVar* var,
              const std::map<std::string, std::string>* initial_values) {
    const cvar::ConfigVarEditorInfo* info = var->editor_info();
    const char* display_name =
        info && info->display_name ? info->display_name : nullptr;

    auto* row = new wxBoxSizer(wxHORIZONTAL);
    auto* label = new wxStaticText(
        scrolled, wxID_ANY, WxLabel(display_name ? display_name : var->name()));
    label->SetMinSize(wxSize(FromDIP(kConfigNamePx), -1));
    // The config key is what the file stores, so it stays discoverable even
    // when a friendly name is shown instead of it.
    std::string tooltip =
        "Section: " + var->category() + "\n\n" + var->description();
    if (display_name) {
      tooltip += "\n\n(config key: " + var->name() + ")";
    }
    label->SetToolTip(WxLabel(tooltip));
    row->Add(label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

    // What this title inherits, so an override is visibly a change rather than
    // the only value on screen.
    auto entry = std::make_unique<GameRow>();
    entry->global_text = var->display_value();

    // Sits where the inherited value used to be printed: the value itself
    // lives in the tooltip, and clicking restores it into the control.
    GameRow* row_ptr = entry.get();
    auto* use_global = new wxButton(scrolled, wxID_ANY, "Use global");
    const std::string global_label =
        ChoiceLabelForValue(var, entry->global_text);
    std::string global_tip = "Global value: " + global_label;
    if (global_label != entry->global_text) {
      global_tip += " (" + entry->global_text + ")";
    }
    global_tip += ".\n\nClick to drop the override and use it for this title.";
    use_global->SetToolTip(WxLabel(global_tip));
    // Putting the global value back into the control is all this needs to do:
    // saving compares each row against the global value and drops the rows
    // that match it.
    use_global->Bind(wxEVT_BUTTON, [row_ptr](wxCommandEvent&) {
      SetConfigRowValue(&row_ptr->config, row_ptr->global_text);
    });
    row->Add(use_global, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

    const std::optional<std::string> override_value =
        var->game_config_display_value();
    // Rebuilds (view toggle, search typing) keep unsaved edits by seeding
    // controls from the collected values instead of the layers.
    std::string current = override_value ? *override_value : entry->global_text;
    if (initial_values) {
      const auto it = initial_values->find(var->name());
      if (it != initial_values->end()) {
        current = it->second;
      }
    }
    BuildConfigRow(var, scrolled, this, row, &entry->config, current);

    rows_.push_back(std::move(entry));
    col->Add(row, 0, wxEXPAND | wxALL, FromDIP(2));
  }

  // Rows the Simple view hides keep their overrides: the save path only
  // touches what is on screen, and the writer emits the whole per-game layer.
  void OnSave(wxCommandEvent&) {
    // Validate before applying anything: a rejected row must not leave the
    // layer half-updated for the writer to pick up.
    std::vector<std::string> invalid;
    for (const auto& entry : rows_) {
      cvar::IConfigVar* var = entry->config.var;
      const std::string text = ConfigRowValue(entry->config);
      if (text == entry->global_text) {
        continue;
      }
      // Only free-form text can be wrong - dropdowns, sliders and flag boxes
      // produce values from fixed sets.
      const bool valid = cvar::IsValidConfigValueText(var->value_kind(), text);
      if (entry->config.text) {
        entry->config.text->SetBackgroundColour(valid ? wxNullColour
                                                      : ErrorBgColour());
        entry->config.text->Refresh();
      }
      if (!valid) {
        invalid.push_back(var->category() + "." + var->name());
      }
    }
    if (!invalid.empty()) {
      std::string message = "Invalid values (not saved):\n";
      for (const auto& name : invalid) {
        message += "\n" + name;
      }
      wxMessageBox(WxLabel(message), "Game config", wxOK | wxICON_ERROR, this);
      return;
    }
    for (const auto& entry : rows_) {
      cvar::IConfigVar* var = entry->config.var;
      const std::string text = ConfigRowValue(entry->config);
      // A row left equal to the global value is not an override, so it is
      // dropped rather than written.
      if (text == entry->global_text) {
        var->ResetGameConfigValue();
      } else {
        var->SetGameConfigValueFromString(text);
      }
    }
    if (!config::SaveGameConfigSparse(title_id_, title_name_, nullptr)) {
      wxMessageBox("Failed to write the game config file.", "Game config",
                   wxOK | wxICON_ERROR, this);
      return;
    }
    wxMessageBox(
        "Game config saved.\n\nIt is applied the next time this title is "
        "started.",
        "Game config", wxOK | wxICON_INFORMATION, this);
  }

  void OnClearOverrides(wxCommandEvent&) {
    if (wxMessageBox(
            "Delete every override for this title and use the global config "
            "instead?",
            "Game config", wxOK | wxCANCEL | wxICON_WARNING, this) != wxOK) {
      return;
    }
    // No overrides left in the layer, so the writer deletes the file.
    config::ClearGameConfig();
    if (!config::SaveGameConfigSparse(title_id_, title_name_, nullptr)) {
      wxMessageBox("Failed to remove the game config file.", "Game config",
                   wxOK | wxICON_ERROR, this);
      return;
    }
    BuildPages(nullptr);
    book_->InvalidateBestSize();
    Layout();
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
        ToLower(TrimConfigText(search_->GetValue().ToStdString()));
    if (query == search_query_) {
      return;
    }
    search_query_ = query;
    const std::map<std::string, std::string> values = CollectValues();
    BuildPages(&values);
    book_->InvalidateBestSize();
    Layout();
  }

  std::string title_id_;
  std::string title_name_;
  std::vector<cvar::IConfigVar*> vars_;
  std::vector<std::unique_ptr<GameRow>> rows_;
  // Pages registered by BuildPages but not built yet, keyed by page index.
  std::map<int, DeferredPage> deferred_pages_;
  // Unsaved edits captured by the last BuildPages call; seeds pages that are
  // built after it.
  std::map<std::string, std::string> seed_values_;
  bool simple_view_ = true;
  std::string search_query_;
  wxTreebook* book_ = nullptr;
  wxSearchCtrl* search_ = nullptr;
  wxCheckBox* simple_check_ = nullptr;
  wxButton* clear_button_ = nullptr;
  wxButton* save_button_ = nullptr;
  wxButton* close_button_ = nullptr;
  wxTimer search_timer_;
};

}  // namespace

void ShowGameConfigDialog(WxWindow* window, const std::string& title_id,
                          const std::string& title_name) {
  if (!window || title_id.empty()) {
    return;
  }
  wxWindow* parent = window->view() ? static_cast<wxWindow*>(window->view())
                                    : static_cast<wxWindow*>(window->frame());
  if (!parent) {
    return;
  }
  WxGameConfigDialog dialog(parent, title_id,
                            title_name.empty() ? title_id : title_name);
  dialog.ShowModal();
  // The dialog loads this title's overrides into the variables so it can show
  // them; on close they go away again, because an override only applies while
  // its own title is running. The file is already written by then, so the next
  // launch reads it back.
  config::ClearGameConfig();
}

}  // namespace wx_ui
}  // namespace app
}  // namespace xe
