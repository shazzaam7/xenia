/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_WX_LIBRARY_VIEW_H_
#define XENIA_APP_WX_LIBRARY_VIEW_H_

#include <filesystem>
#include <string>
#include <vector>

#include <wx/bitmap.h>
#include <wx/event.h>
#include <wx/imaglist.h>
#include <wx/listctrl.h>
#include <wx/panel.h>
#include <wx/simplebook.h>
#include <wx/srchctrl.h>
#include <wx/stattext.h>

#include "xenia/app/wx/wx_compat_db.h"
#include "xenia/app/wx/wx_game_model.h"

namespace xe {
namespace app {
namespace wx_ui {

// Display-only game library: one model, wxListCtrl table + grid, search
// filter, title/last-played/status sort. Importing and persistence live in
// wx_library_store / wx_game_scan / wx_game_art; booting lives with the
// owner (EmulatorWindow) via Delegate.
class WxLibraryView : public wxPanel {
 public:
  struct Delegate {
    virtual ~Delegate() = default;
    virtual void OnBootGame(size_t index, int disc_number) = 0;
    virtual void OnRemoveGame(size_t index) = 0;
    virtual void OnShowInFolder(size_t index) = 0;
    virtual void OnAddGame() = 0;
    virtual void OnScanFolder() = 0;
    virtual void OnProfileMenu() = 0;
    virtual void OnViewContent(size_t index) = 0;
    // Per-title config overrides (the game library's right-click entry).
    virtual void OnGameConfig(size_t index) = 0;
    // Per-title patch editor (the game library's right-click entry).
    virtual void OnPatches(size_t index) = 0;
    // Per-title info editor: name, discs, artwork (right-click entry).
    virtual void OnEditGame(size_t index) = 0;
    // Open the compatibility report for a title with one.
    virtual void OnViewCompatReport(size_t index) = 0;
    // Open an issue search for a title without a report.
    virtual void OnSearchCompatIssues(size_t index) = 0;
  };

  WxLibraryView(wxWindow* parent, Delegate* delegate,
                const std::filesystem::path& storage_root);

  void SetEntries(std::vector<GameEntry> entries);
  const std::vector<GameEntry>& entries() const { return entries_; }
  // Re-scales icons, balls, and columns for the current monitor DPI and
  // repopulates. Called on DPI changes; image lists are recreated because
  // they cannot resize in place.
  void RefreshDpi();

 private:
  enum : int {
    kColIcon = 0,
    kColStatus,
    kColTitleId,
    kColTitle,
    kColLocation,
    kColLastPlayed,
  };

  void RebuildIcons();
  void Populate();
  void ApplySort();
  void ApplyDpi();
  void ApplyColumnWidths();
  void IconFor(const GameEntry& entry, int* small_out, int* big_out);
  CompatRating EntryRating(size_t index) const;
  std::string LastPlayedLabel(std::time_t t) const;
  size_t ViewSelection(wxListCtrl* view) const;
  void BootFrom(wxListCtrl* view);
  int PickDisc(const GameEntry& entry);
  void ShowContext(wxListCtrl* view, const wxPoint& pos);

  void OnSearch(wxCommandEvent& event);
  void OnSearchCancel(wxCommandEvent& event);
  void OnMode(wxCommandEvent& event);
  void OnSortColumn(wxListEvent& event);
  void OnHoverTable(wxMouseEvent& event);
  void OnHoverGrid(wxMouseEvent& event);
  void OnSysColourChanged(wxSysColourChangedEvent& event);
  void OnActivate(wxListEvent& event);
  void OnContextTable(wxListEvent& event);
  void OnContextGrid(wxListEvent& event);
  void OnAdd(wxCommandEvent& event);
  void OnScan(wxCommandEvent& event);
  void OnProfile(wxCommandEvent& event);
  void OnMenu(wxCommandEvent& event);

  Delegate* delegate_;
  std::filesystem::path storage_root_;
  std::vector<GameEntry> entries_;
  // Row order for the table (SortItems-free resort via repopulate).
  std::vector<size_t> order_;
  std::string filter_;
  int sort_column_ = kColTitle;
  bool sort_ascending_ = true;
  bool grid_mode_ = false;

  wxListCtrl* table_ = nullptr;
  wxListCtrl* grid_ = nullptr;
  wxSimplebook* book_ = nullptr;
  wxSearchCtrl* search_ = nullptr;
  wxButton* profile_button_ = nullptr;
  wxStaticText* empty_hint_ = nullptr;
  wxImageList* small_images_ = nullptr;
  wxImageList* big_images_ = nullptr;
  // Icon/badge sizes in physical pixels for the current monitor DPI,
  // refreshed by RefreshDpi (image lists are fixed-size and recreated).
  int small_icon_px_ = 32;
  int big_icon_px_ = 128;
  int status_ball_px_ = 16;
  int grid_ball_px_ = 12;
  int grid_inset_px_ = 3;
  // entry index -> image list position (small_images_).
  std::vector<int> icon_index_;
  // entry index -> image list position (big_images_, grid view). Tracked
  // separately because the small list also holds the status balls, so the
  // two lists number entry icons differently.
  std::vector<int> big_icon_index_;
  // Compatibility data (Unknown when an entry has no report) and the
  // pre-rendered status balls (index = rating rank). Balls occupy
  // small_images_ slots 1..5, right after the placeholder.
  // Pre-rendered status balls (index = rating rank). Balls occupy
  // small_images_ slots 1..5, right after the placeholder.
  wxBitmap compat_balls_[5];
  wxBitmap compat_grid_balls_[5];
  // Last hover tooltip, to avoid resetting it on every mouse move.
  wxString last_tip_;
  size_t menu_index_ = 0;
};

// Imports disc paths into entries (skipping paths already present with art),
// reading metadata + writing artwork, with a cancellable progress dialog.
// Returns true when the library changed.
bool ImportGamePaths(wxWindow* parent,
                     const std::filesystem::path& storage_root,
                     std::vector<GameEntry>& entries,
                     const std::vector<std::filesystem::path>& paths);

}  // namespace wx_ui
}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_WX_LIBRARY_VIEW_H_
