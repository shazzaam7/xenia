/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/app/wx/wx_library_view.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <sstream>

#include <wx/bitmap.h>
#include <wx/button.h>
#include <wx/choicdlg.h>
#include <wx/choice.h>
#include <wx/colour.h>
#include <wx/dcmemory.h>
#include <wx/dirdlg.h>
#include <wx/filedlg.h>
#include <wx/font.h>
#include <wx/imaglist.h>
#include <wx/listctrl.h>
#include <wx/menu.h>
#include <wx/progdlg.h>
#include <wx/simplebook.h>
#include <wx/sizer.h>
#include <wx/srchctrl.h>
#include <wx/stattext.h>

#include "xenia/app/wx/wx_compat_db.h"
#include "xenia/app/wx/wx_game_art.h"
#include "xenia/app/wx/wx_game_scan.h"
#include "xenia/app/wx/wx_library_store.h"
#include "xenia/app/wx/wx_util.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"

namespace xe {
namespace app {
namespace wx_ui {

namespace {

constexpr int kSmallIconPx = 32;
constexpr int kBigIconPx = 128;
// Status ball diameter: fits the 32px row with padding.
constexpr int kStatusBallPx = 16;
// Grid badge diameter and corner inset.
constexpr int kGridBallPx = 24;
constexpr int kGridBallInsetPx = 6;

enum : int {
  kIdSearch = wxID_HIGHEST + 100,
  kIdMode,
  kIdAdd,
  kIdScan,
  kIdProfile,
  kIdMenuBoot,
  kIdMenuDisc,
  kIdMenuFolder,
  kIdMenuRemove,
  kIdMenuContent,
  kIdMenuConfig,
  kIdMenuPatches,
  kIdMenuCompatReport,
  kIdMenuCompatSearch,
};

bool MatchesFilter(const GameEntry& entry, const std::string& filter) {
  if (filter.empty()) {
    return true;
  }
  auto lower = filter;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return char(std::tolower(c)); });
  auto contains = [&](const std::string& hay) {
    std::string h = hay;
    std::transform(h.begin(), h.end(), h.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return h.find(lower) != std::string::npos;
  };
  return contains(entry.name) || contains(entry.title_id);
}

wxBitmap PlaceholderBitmap(int px) {
  wxBitmap bmp(px, px, 32);
  wxMemoryDC dc(bmp);
  dc.SetBackground(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE)));
  dc.Clear();
  dc.SetTextForeground(
      wxColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT)));
  dc.SetFont(wxFont(px / 3, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL,
                    wxFONTWEIGHT_BOLD));
  dc.DrawLabel("?", wxRect(0, 0, px, px), wxALIGN_CENTER);
  dc.SelectObject(wxNullBitmap);
  return bmp;
}

}  // namespace

WxLibraryView::WxLibraryView(wxWindow* parent, Delegate* delegate,
                             const std::filesystem::path& storage_root)
    : wxPanel(parent, wxID_ANY),
      delegate_(delegate),
      storage_root_(storage_root) {
  wxInitAllImageHandlers();

  auto bar = new wxBoxSizer(wxHORIZONTAL);
  auto add = new wxButton(this, kIdAdd, "Add Game");
  auto scan = new wxButton(this, kIdScan, "Scan Folder");
  auto mode = new wxChoice(this, kIdMode);
  mode->Append("List");
  mode->Append("Grid");
  mode->SetSelection(0);
  search_ = new wxSearchCtrl(this, kIdSearch);
  search_->SetHint("Search library");
  search_->ShowCancelButton(true);
  bar->Add(add, 0, wxRIGHT, FromDIP(8));
  bar->Add(scan, 0, wxRIGHT, FromDIP(8));
  bar->Add(mode, 0, wxRIGHT, FromDIP(8));
  bar->Add(search_, 1, wxEXPAND);
  profile_button_ = new wxButton(this, kIdProfile, "Profile");
  bar->Add(profile_button_, 0, wxLEFT, FromDIP(8));

  book_ = new wxSimplebook(this, wxID_ANY);
  table_ = new wxListCtrl(book_, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                          wxLC_REPORT | wxLC_SINGLE_SEL);
  grid_ = new wxListCtrl(book_, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                         wxLC_ICON | wxLC_SINGLE_SEL | wxLC_AUTOARRANGE);
  book_->AddPage(table_, "List");
  book_->AddPage(grid_, "Grid");

  empty_hint_ = new wxStaticText(this, wxID_ANY,
                                 "No games yet. Use Add Game or Scan Folder.");

  auto sizer = new wxBoxSizer(wxVERTICAL);
  sizer->Add(bar, 0, wxEXPAND | wxALL, FromDIP(8));
  sizer->Add(book_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));
  sizer->Add(empty_hint_, 0, wxALIGN_CENTER | wxBOTTOM, FromDIP(8));
  SetSizer(sizer);

  ApplyDpi();
  small_images_ = new wxImageList(small_icon_px_, small_icon_px_, true);
  big_images_ = new wxImageList(big_icon_px_, big_icon_px_, true);
  table_->AssignImageList(small_images_, wxIMAGE_LIST_SMALL);
  grid_->AssignImageList(big_images_, wxIMAGE_LIST_NORMAL);

  Bind(wxEVT_SEARCH, &WxLibraryView::OnSearch, this, kIdSearch);
  Bind(wxEVT_TEXT, &WxLibraryView::OnSearch, this, kIdSearch);
  Bind(wxEVT_SEARCH_CANCEL, &WxLibraryView::OnSearchCancel, this, kIdSearch);
  Bind(wxEVT_CHOICE, &WxLibraryView::OnMode, this, kIdMode);
  Bind(wxEVT_BUTTON, &WxLibraryView::OnAdd, this, kIdAdd);
  Bind(wxEVT_BUTTON, &WxLibraryView::OnScan, this, kIdScan);
  Bind(wxEVT_BUTTON, &WxLibraryView::OnProfile, this, kIdProfile);
  Bind(wxEVT_MENU, &WxLibraryView::OnMenu, this, kIdMenuBoot,
       kIdMenuCompatSearch);
  table_->Bind(wxEVT_LIST_COL_CLICK, &WxLibraryView::OnSortColumn, this);
  table_->Bind(wxEVT_MOTION, &WxLibraryView::OnHoverTable, this);
  grid_->Bind(wxEVT_MOTION, &WxLibraryView::OnHoverGrid, this);
  // OS theme flips rebuild theme-baked bitmaps through the DPI path (sizes
  // recompute identically; colors pick up the new system palette).
  Bind(wxEVT_SYS_COLOUR_CHANGED, &WxLibraryView::OnSysColourChanged, this);
  table_->Bind(wxEVT_LIST_ITEM_ACTIVATED, &WxLibraryView::OnActivate, this);
  grid_->Bind(wxEVT_LIST_ITEM_ACTIVATED, &WxLibraryView::OnActivate, this);
  table_->Bind(wxEVT_LIST_ITEM_RIGHT_CLICK, &WxLibraryView::OnContextTable,
               this);
  grid_->Bind(wxEVT_LIST_ITEM_RIGHT_CLICK, &WxLibraryView::OnContextGrid, this);
}

void WxLibraryView::SetEntries(std::vector<GameEntry> entries) {
  entries_ = std::move(entries);
  RebuildIcons();
  Populate();
}

void WxLibraryView::ApplyDpi() {
  small_icon_px_ = FromDIP(kSmallIconPx);
  big_icon_px_ = FromDIP(kBigIconPx);
  status_ball_px_ = FromDIP(kStatusBallPx);
  grid_ball_px_ = FromDIP(kGridBallPx);
  grid_inset_px_ = FromDIP(kGridBallInsetPx);
  for (size_t i = 0; i < 5; ++i) {
    compat_balls_[i] =
        MakeCompatBall(static_cast<CompatRating>(i), status_ball_px_);
    compat_grid_balls_[i] =
        MakeCompatBall(static_cast<CompatRating>(i), grid_ball_px_);
  }
  ApplyColumnWidths();
}

void WxLibraryView::ApplyColumnWidths() {
  const struct {
    int id;
    const char* label;
    int width;
  } columns[] = {
      {kColIcon, "", 40},
      {kColStatus, "", 40},
      {kColTitleId, "Title ID", 90},
      {kColMediaId, "Media ID", 90},
      {kColTitle, "Title", 260},
      {kColLocation, "Location", 320},
      {kColLastPlayed, "Last Played", 140},
  };
  for (const auto& column : columns) {
    const int width = FromDIP(column.width);
    if (table_->GetColumnCount() <= column.id) {
      table_->InsertColumn(column.id, column.label, wxLIST_FORMAT_LEFT, width);
    } else {
      table_->SetColumnWidth(column.id, width);
    }
  }
}

void WxLibraryView::RefreshDpi() {
  ApplyDpi();
  // Image lists are fixed-size: recreate and reassign (AssignImageList
  // deletes the previous lists).
  small_images_ = new wxImageList(small_icon_px_, small_icon_px_, true);
  big_images_ = new wxImageList(big_icon_px_, big_icon_px_, true);
  table_->AssignImageList(small_images_, wxIMAGE_LIST_SMALL);
  grid_->AssignImageList(big_images_, wxIMAGE_LIST_NORMAL);
  RebuildIcons();
  Populate();
  Layout();
}

void WxLibraryView::RebuildIcons() {
  small_images_->RemoveAll();
  big_images_->RemoveAll();
  small_images_->Add(PlaceholderBitmap(small_icon_px_));
  // Status balls occupy slots 1..5 so SetItemColumnImage can address them
  // by rating rank; entry icons follow. The list requires icon-sized
  // bitmaps, so the balls are centered on transparent canvases.
  for (size_t i = 0; i < 5; ++i) {
    wxImage canvas(small_icon_px_, small_icon_px_);
    canvas.SetAlpha();
    std::memset(canvas.GetAlpha(), 0,
                static_cast<size_t>(small_icon_px_) * small_icon_px_);
    wxBitmap padded(canvas);
    {
      wxMemoryDC dc(padded);
      const int off = (small_icon_px_ - status_ball_px_) / 2;
      dc.DrawBitmap(compat_balls_[i], off, off, true);
      dc.SelectObject(wxNullBitmap);
    }
    small_images_->Add(padded);
  }
  big_images_->Add(PlaceholderBitmap(big_icon_px_));
  icon_index_.assign(entries_.size(), 0);
  big_icon_index_.assign(entries_.size(), 0);
  for (size_t i = 0; i < entries_.size(); i++) {
    IconFor(entries_[i], &icon_index_[i], &big_icon_index_[i]);
  }
}

CompatRating WxLibraryView::EntryRating(size_t index) const {
  // Ratings persist on the entry itself (library.toml); empty means Unknown.
  return index < entries_.size() ? CompatRatingFromId(entries_[index].compat)
                                 : CompatRating::kUnknown;
}

void WxLibraryView::IconFor(const GameEntry& entry, int* small_out,
                            int* big_out) {
  if (entry.title_id.empty()) {
    *small_out = 0;
    *big_out = 0;
    return;
  }
  auto path = ArtIconPath(storage_root_, entry.title_id);
  std::error_code ec = {};
  if (!std::filesystem::exists(path, ec)) {
    *small_out = 0;
    *big_out = 0;
    return;
  }
  wxImage image;
  if (!image.LoadFile(wxString::FromUTF8(xe::path_to_utf8(path))) ||
      !image.IsOk()) {
    *small_out = 0;
    *big_out = 0;
    return;
  }
  int small_idx = small_images_->Add(wxBitmap(
      image.Scale(small_icon_px_, small_icon_px_, wxIMAGE_QUALITY_HIGH)));
  // Corner badge with the entry's compatibility rating.
  wxBitmap big(image.Scale(big_icon_px_, big_icon_px_, wxIMAGE_QUALITY_HIGH));
  {
    const wxBitmap& ball = compat_grid_balls_[static_cast<size_t>(
        CompatRatingFromId(entry.compat))];
    wxMemoryDC dc(big);
    dc.DrawBitmap(ball, big_icon_px_ - ball.GetWidth() - grid_inset_px_,
                  big_icon_px_ - ball.GetHeight() - grid_inset_px_, true);
    dc.SelectObject(wxNullBitmap);
  }
  int big_idx = big_images_->Add(big);
  *small_out = small_idx;
  *big_out = big_idx;
}

std::string WxLibraryView::LastPlayedLabel(std::time_t t) const {
  if (!t) {
    return "Never";
  }
  std::tm tm = {};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  std::ostringstream ss;
  ss << std::put_time(&tm, "%Y-%m-%d %H:%M");
  return ss.str();
}

void WxLibraryView::ApplySort() {
  order_.clear();
  for (size_t i = 0; i < entries_.size(); i++) {
    if (MatchesFilter(entries_[i], filter_)) {
      order_.push_back(i);
    }
  }
  auto by_title = [&](size_t a, size_t b) {
    std::string x = entries_[a].name, y = entries_[b].name;
    std::transform(x.begin(), x.end(), x.begin(), ::tolower);
    std::transform(y.begin(), y.end(), y.begin(), ::tolower);
    return sort_ascending_ ? x < y : x > y;
  };
  auto by_played = [&](size_t a, size_t b) {
    return sort_ascending_ ? entries_[a].last_play < entries_[b].last_play
                           : entries_[a].last_play > entries_[b].last_play;
  };
  // Best-first on ascending click (Xenia Manager parity); ties fall back to
  // the title order below.
  auto by_status = [&](size_t a, size_t b) {
    auto ra = static_cast<uint8_t>(EntryRating(a));
    auto rb = static_cast<uint8_t>(EntryRating(b));
    return ra != rb ? (sort_ascending_ ? ra > rb : ra < rb) : by_title(a, b);
  };
  // Grid always stays name-sorted (RPCS3 parity); the table follows the
  // clicked column.
  if (grid_mode_) {
    bool asc = sort_ascending_;
    sort_ascending_ = true;
    std::sort(order_.begin(), order_.end(), by_title);
    sort_ascending_ = asc;
  } else if (sort_column_ == kColLastPlayed) {
    std::sort(order_.begin(), order_.end(), by_played);
  } else if (sort_column_ == kColStatus) {
    std::sort(order_.begin(), order_.end(), by_status);
  } else {
    std::sort(order_.begin(), order_.end(), by_title);
  }
}

void WxLibraryView::Populate() {
  ApplySort();
  table_->DeleteAllItems();
  grid_->DeleteAllItems();
  long row = 0;
  for (size_t index : order_) {
    const auto& e = entries_[index];
    int icon = index < icon_index_.size() ? icon_index_[index] : 0;
    int big_icon = index < big_icon_index_.size() ? big_icon_index_[index] : 0;
    long item = table_->InsertItem(row, icon);
    table_->SetItemColumnImage(item, kColStatus,
                               1 + static_cast<int>(EntryRating(index)));
    table_->SetItem(item, kColTitleId, WxLabel(e.title_id));
    table_->SetItem(item, kColMediaId, WxLabel(e.MediaIdLabel()));
    table_->SetItem(item, kColTitle, WxLabel(e.name));
    table_->SetItem(item, kColLocation, WxLabel(e.LocationLabel()));
    table_->SetItem(item, kColLastPlayed,
                    WxLabel(LastPlayedLabel(e.last_play)));
    table_->SetItemData(item, index);
    grid_->InsertItem(grid_->GetItemCount(), WxLabel(e.name), big_icon);
    grid_->SetItemData(grid_->GetItemCount() - 1, index);
    row++;
  }
  empty_hint_->Show(entries_.empty());
  Layout();
}

size_t WxLibraryView::ViewSelection(wxListCtrl* view) const {
  long sel = view->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
  if (sel < 0) {
    return SIZE_MAX;
  }
  return size_t(view->GetItemData(sel));
}

int WxLibraryView::PickDisc(const GameEntry& entry) {
  if (!entry.IsMultiDisc()) {
    return 1;
  }
  wxArrayString choices;
  for (const auto& disc : entry.discs) {
    std::string label = "Disc " + std::to_string(disc.number);
    if (!disc.label.empty() && disc.label != label) {
      label += " (" + disc.label + ")";
    }
    choices.Add(WxLabel(label));
  }
  int initial = 0;
  for (size_t i = 0; i < entry.discs.size(); i++) {
    if (entry.discs[i].number == entry.last_played_disc) {
      initial = int(i);
    }
  }
  wxSingleChoiceDialog dialog(this, WxLabel("Select disc for " + entry.name),
                              "Multi-disc game", choices);
  dialog.SetSelection(initial);
  if (dialog.ShowModal() != wxID_OK) {
    return -1;
  }
  int sel = dialog.GetSelection();
  return sel >= 0 && size_t(sel) < entry.discs.size()
             ? entry.discs[size_t(sel)].number
             : -1;
}

void WxLibraryView::BootFrom(wxListCtrl* view) {
  size_t index = ViewSelection(view);
  if (index == SIZE_MAX || index >= entries_.size() || !delegate_) {
    return;
  }
  int disc = PickDisc(entries_[index]);
  if (disc > 0) {
    delegate_->OnBootGame(index, disc);
  }
}

void WxLibraryView::ShowContext(wxListCtrl* view, const wxPoint& pos) {
  int flags = 0;
  long hit = view->HitTest(pos, flags);
  if (hit < 0) {
    return;
  }
  view->SetItemState(hit, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
  menu_index_ = size_t(view->GetItemData(hit));
  if (menu_index_ >= entries_.size()) {
    return;
  }
  wxMenu menu;
  menu.Append(kIdMenuBoot, "Boot");
  if (entries_[menu_index_].IsMultiDisc()) {
    menu.Append(kIdMenuDisc, "Select Disc and Boot...");
  }
  menu.AppendSeparator();
  menu.Append(kIdMenuConfig, "Game Config...");
  menu.Append(kIdMenuPatches, "Patches...");
  const auto& entry = entries_[menu_index_];
  if (!entry.compat_url.empty()) {
    menu.Append(kIdMenuCompatReport, "View Compatibility Report...");
  } else if (!entry.title_id.empty()) {
    menu.Append(kIdMenuCompatSearch, "Search Compatibility Issues...");
  }
  menu.AppendSeparator();
  menu.Append(kIdMenuFolder, "Show in Folder");
  menu.Append(kIdMenuRemove, "Remove");
  menu.AppendSeparator();
  menu.Append(kIdMenuContent, "View Content...");
  view->PopupMenu(&menu);
}

void WxLibraryView::OnSearch(wxCommandEvent& event) {
  filter_ = event.GetString().ToStdString();
  Populate();
}

void WxLibraryView::OnSearchCancel(wxCommandEvent&) {
  filter_.clear();
  search_->Clear();
  Populate();
}

void WxLibraryView::OnMode(wxCommandEvent& event) {
  grid_mode_ = event.GetSelection() == 1;
  book_->ChangeSelection(grid_mode_ ? 1 : 0);
  Populate();
}

void WxLibraryView::OnSortColumn(wxListEvent& event) {
  int col = event.GetColumn();
  if (col != kColTitle && col != kColLastPlayed && col != kColStatus) {
    return;
  }
  if (sort_column_ == col) {
    sort_ascending_ = !sort_ascending_;
  } else {
    sort_column_ = col;
    sort_ascending_ = true;
  }
  Populate();
}

void WxLibraryView::OnHoverTable(wxMouseEvent& event) {
  // wxListCtrl has no per-cell tooltips: hit-test the status column and
  // drive the control tooltip manually. Cached so it only resets on change.
  int flags = 0;
  long sub = 0;
  long row = table_->HitTest(event.GetPosition(), flags, &sub);
  wxString tip;
  if (row >= 0 && sub == kColStatus) {
    size_t index = size_t(table_->GetItemData(row));
    if (index < entries_.size()) {
      tip = CompatName(EntryRating(index));
    }
  }
  if (tip != last_tip_) {
    last_tip_ = tip;
    if (tip.empty()) {
      table_->UnsetToolTip();
    } else {
      table_->SetToolTip(tip);
    }
  }
  event.Skip();
}

void WxLibraryView::OnSysColourChanged(wxSysColourChangedEvent& event) {
  RefreshDpi();
  event.Skip();
}

void WxLibraryView::OnHoverGrid(wxMouseEvent& event) {
  int flags = 0;
  long row = grid_->HitTest(event.GetPosition(), flags);
  wxString tip;
  if (row >= 0) {
    size_t index = size_t(grid_->GetItemData(row));
    if (index < entries_.size()) {
      tip = CompatName(EntryRating(index));
    }
  }
  if (tip != last_tip_) {
    last_tip_ = tip;
    if (tip.empty()) {
      grid_->UnsetToolTip();
    } else {
      grid_->SetToolTip(tip);
    }
  }
  event.Skip();
}

void WxLibraryView::OnActivate(wxListEvent& event) {
  auto* view = static_cast<wxListCtrl*>(event.GetEventObject());
  BootFrom(view ? view : table_);
}

void WxLibraryView::OnContextTable(wxListEvent& event) {
  ShowContext(table_, event.GetPoint());
}

void WxLibraryView::OnContextGrid(wxListEvent& event) {
  ShowContext(grid_, event.GetPoint());
}

void WxLibraryView::OnAdd(wxCommandEvent&) {
  if (delegate_) {
    delegate_->OnAddGame();
  }
}

void WxLibraryView::OnScan(wxCommandEvent&) {
  if (delegate_) {
    delegate_->OnScanFolder();
  }
}

void WxLibraryView::OnProfile(wxCommandEvent&) {
  if (delegate_) {
    delegate_->OnProfileMenu();
  }
}

void WxLibraryView::OnMenu(wxCommandEvent& event) {
  if (!delegate_ || menu_index_ >= entries_.size()) {
    return;
  }
  switch (event.GetId()) {
    case kIdMenuBoot: {
      int disc = entries_[menu_index_].IsMultiDisc()
                     ? PickDisc(entries_[menu_index_])
                     : 1;
      if (disc > 0) {
        delegate_->OnBootGame(menu_index_, disc);
      }
      break;
    }
    case kIdMenuDisc: {
      int disc = PickDisc(entries_[menu_index_]);
      if (disc > 0) {
        delegate_->OnBootGame(menu_index_, disc);
      }
      break;
    }
    case kIdMenuFolder:
      delegate_->OnShowInFolder(menu_index_);
      break;
    case kIdMenuRemove:
      delegate_->OnRemoveGame(menu_index_);
      break;
    case kIdMenuContent:
      delegate_->OnViewContent(menu_index_);
      break;
    case kIdMenuConfig:
      delegate_->OnGameConfig(menu_index_);
      break;
    case kIdMenuPatches:
      delegate_->OnPatches(menu_index_);
      break;
    case kIdMenuCompatReport:
      delegate_->OnViewCompatReport(menu_index_);
      break;
    case kIdMenuCompatSearch:
      delegate_->OnSearchCompatIssues(menu_index_);
      break;
    default:
      break;
  }
}

// Art cache v3: v2 plus nxeart art that can actually be read (the STFS mount
// used to point at the metadata region, so backgrounds/slots were carved out
// of raw bytes or came from an unrelated package) and container-root nxeart
// lookups. Older caches refresh once by reprocessing the scan batch, then this
// stamps.
constexpr char kArtCacheVersion = '3';

bool ArtCacheCurrent(const std::filesystem::path& storage_root) {
  FILE* f = xe::filesystem::OpenFile(
      storage_root / "artwork" / ".cache-version", "rb");
  if (!f) {
    return false;
  }
  int c = std::fgetc(f);
  std::fclose(f);
  return c == kArtCacheVersion;
}

void StampArtCache(const std::filesystem::path& storage_root) {
  auto marker = storage_root / "artwork" / ".cache-version";
  std::error_code ec = {};
  std::filesystem::create_directories(marker.parent_path(), ec);
  FILE* f = xe::filesystem::OpenFile(marker, "wb");
  if (f) {
    std::fputc(kArtCacheVersion, f);
    std::fclose(f);
  }
}

bool ImportGamePaths(wxWindow* parent,
                     const std::filesystem::path& storage_root,
                     std::vector<GameEntry>& entries,
                     const std::vector<std::filesystem::path>& paths) {
  if (paths.empty()) {
    return false;
  }
  bool refresh_art = !ArtCacheCurrent(storage_root);
  auto already_have = [&](const std::filesystem::path& p) {
    if (refresh_art) {
      return false;
    }
    for (const auto& e : entries) {
      for (const auto& d : e.discs) {
        std::error_code ec = {};
        if (std::filesystem::equivalent(d.path, p, ec)) {
          auto icon = ArtIconPath(storage_root, e.title_id);
          if (std::filesystem::exists(icon, ec)) {
            return true;
          }
        }
      }
    }
    return false;
  };
  wxProgressDialog progress("Scanning games", "Reading game files...",
                            int(paths.size()), parent,
                            wxPD_APP_MODAL | wxPD_CAN_ABORT | wxPD_AUTO_HIDE);
  bool changed = false;
  bool completed = true;
  for (size_t i = 0; i < paths.size(); i++) {
    if (!progress.Update(int(i),
                         WxLabel(xe::path_to_utf8(paths[i].filename())))) {
      completed = false;
      break;  // Cancelled.
    }
    const auto& path = paths[i];
    if (already_have(path)) {
      continue;
    }
    GameMeta meta;
    if (!ReadGameMeta(path, meta) || !meta.ok || meta.title_id.empty() ||
        meta.title_id == "00000000") {
      XELOGW("Library: skipping unrecognized file {}", xe::path_to_utf8(path));
      continue;
    }
    std::string name =
        meta.name.empty() ? xe::path_to_utf8(path.stem()) : meta.name;
    MergeScannedGame(entries, path, meta.title_id, meta.media_id, name);
    EnsureArtwork(storage_root, path, meta.type, meta.icon_bytes,
                  meta.title_id);
    changed = true;
  }
  if (refresh_art && completed) {
    StampArtCache(storage_root);
  }
  return changed;
}

}  // namespace wx_ui
}  // namespace app
}  // namespace xe
