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

#include <wx/event.h>
#include <wx/imaglist.h>
#include <wx/listctrl.h>
#include <wx/panel.h>
#include <wx/simplebook.h>
#include <wx/srchctrl.h>
#include <wx/stattext.h>

#include "xenia/app/wx/wx_game_model.h"

namespace xe {
namespace app {
namespace wx_ui {

// Display-only game library: one model, wxListCtrl table + grid, search
// filter, title/last-played sort. Importing and persistence live in
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
  };

  WxLibraryView(wxWindow* parent, Delegate* delegate,
                const std::filesystem::path& storage_root);

  void SetEntries(std::vector<GameEntry> entries);
  const std::vector<GameEntry>& entries() const { return entries_; }
  void RefreshEntry(size_t index);

 private:
  enum : int {
    kColIcon = 0,
    kColTitleId,
    kColMediaId,
    kColTitle,
    kColLocation,
    kColLastPlayed,
  };

  void RebuildIcons();
  void Populate();
  void ApplySort();
  int IconFor(const GameEntry& entry);
  std::string LastPlayedLabel(std::time_t t) const;
  size_t ViewSelection(wxListCtrl* view) const;
  void BootFrom(wxListCtrl* view);
  int PickDisc(const GameEntry& entry);
  void ShowContext(wxListCtrl* view, const wxPoint& pos);

  void OnSearch(wxCommandEvent& event);
  void OnSearchCancel(wxCommandEvent& event);
  void OnMode(wxCommandEvent& event);
  void OnSortColumn(wxListEvent& event);
  void OnActivate(wxListEvent& event);
  void OnContextTable(wxListEvent& event);
  void OnContextGrid(wxListEvent& event);
  void OnAdd(wxCommandEvent& event);
  void OnScan(wxCommandEvent& event);
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
  wxStaticText* empty_hint_ = nullptr;
  wxImageList* small_images_ = nullptr;
  wxImageList* big_images_ = nullptr;
  // entry index -> image list position.
  std::vector<int> icon_index_;
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
