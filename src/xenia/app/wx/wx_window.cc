/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// Shared plumbing of the wxWidgets backend (WxWindow hosts a frame with a
// menu bar and a view panel for presenting). Platform specifics live in
// wx_window_win.cc / wx_window_linux.cc.

#include "xenia/app/wx/wx_window.h"

#include "xenia/app/wx/wx_util.h"
#include "xenia/app/wx/wx_window_priv.h"

#include <wx/app.h>
#include <wx/dcclient.h>
#include <wx/dirdlg.h>
#include <wx/dnd.h>
#include <wx/filedlg.h>
#include <wx/frame.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/timer.h>
#include <wx/utils.h>

#include "xenia/app/wx/wx_game_scan.h"
#include "xenia/app/wx/wx_library_store.h"

#if defined(__WXMSW__)
#include <Dbt.h>
#endif

namespace xe {
namespace app {
namespace wx_ui {

// WxViewPanel forwards native control events to the owning WxWindow, which
// translates them into xe::ui events like the native backends did.
WxViewPanel::WxViewPanel(WxWindow* owner, wxWindow* parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
              wxWANTS_CHARS),
      owner_(owner) {
  SetBackgroundColour(*wxBLACK);
  cursor_timer_.SetOwner(this, kCursorTimerId);
  Bind(wxEVT_PAINT, &WxViewPanel::OnPaint, this);
  Bind(wxEVT_ERASE_BACKGROUND, &WxViewPanel::OnEraseBackground, this);
  Bind(wxEVT_SIZE, &WxViewPanel::OnSize, this);
  Bind(wxEVT_SET_FOCUS, &WxViewPanel::OnFocus, this);
  Bind(wxEVT_KILL_FOCUS, &WxViewPanel::OnFocus, this);
  Bind(wxEVT_KEY_DOWN, &WxViewPanel::OnKeyDown, this);
  Bind(wxEVT_KEY_UP, &WxViewPanel::OnKeyUp, this);
  Bind(wxEVT_CHAR, &WxViewPanel::OnKeyChar, this);
  Bind(wxEVT_LEFT_DOWN, &WxViewPanel::OnMouseDown, this);
  Bind(wxEVT_LEFT_UP, &WxViewPanel::OnMouseUp, this);
  Bind(wxEVT_RIGHT_DOWN, &WxViewPanel::OnMouseDown, this);
  Bind(wxEVT_RIGHT_UP, &WxViewPanel::OnMouseUp, this);
  Bind(wxEVT_MIDDLE_DOWN, &WxViewPanel::OnMouseDown, this);
  Bind(wxEVT_MIDDLE_UP, &WxViewPanel::OnMouseUp, this);
  Bind(wxEVT_AUX1_DOWN, &WxViewPanel::OnMouseDown, this);
  Bind(wxEVT_AUX1_UP, &WxViewPanel::OnMouseUp, this);
  Bind(wxEVT_AUX2_DOWN, &WxViewPanel::OnMouseDown, this);
  Bind(wxEVT_AUX2_UP, &WxViewPanel::OnMouseUp, this);
  Bind(wxEVT_MOTION, &WxViewPanel::OnMouseMove, this);
  Bind(wxEVT_MOUSEWHEEL, &WxViewPanel::OnMouseWheel, this);
  Bind(wxEVT_DPI_CHANGED, &WxViewPanel::OnDpiChanged, this);
  Bind(wxEVT_TIMER, &WxViewPanel::OnCursorTimer, this, kCursorTimerId);
}

void WxViewPanel::DetachOwner() {
  owner_ = nullptr;
  cursor_timer_.Stop();
}
void WxViewPanel::StartCursorTimer() {
  // Same interval as Window::kDefaultCursorAutoHideMilliseconds (not
  // accessible here as it's protected in ui::Window).
  cursor_timer_.StartOnce(3333);
}
void WxViewPanel::StopCursorTimer() { cursor_timer_.Stop(); }

#if defined(__WXMSW__)
WXLRESULT WxViewPanel::MSWWindowProc(WXUINT nMsg, WXWPARAM wParam,
                                     WXLPARAM lParam) {
  if (nMsg == WM_DEVICECHANGE && owner_) {
    if (wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE) {
      owner_->OnWxDeviceChange(wParam == DBT_DEVICEARRIVAL);
      return 0;
    }
  }
  return wxPanel::MSWWindowProc(nMsg, wParam, lParam);
}
#endif

void WxViewPanel::OnPaint(wxPaintEvent&) {
  wxPaintDC dc(this);
  if (owner_) {
    owner_->OnWxPaint();
  }
}
void WxViewPanel::OnEraseBackground(wxEraseEvent& event) {
  // Like WM_ERASEBKGND handling: don't erase between paints when a surface
  // exists, painting may be dropped if nothing changed since the last one.
  if (owner_ && owner_->HasSurface()) {
    return;
  }
  event.Skip();
}
void WxViewPanel::OnSize(wxSizeEvent& event) {
  event.Skip();
  if (owner_) {
    owner_->OnWxSize();
  }
}
void WxViewPanel::OnFocus(wxFocusEvent& event) {
  event.Skip();
  if (owner_) {
    owner_->OnWxFocus(event.GetEventType() == wxEVT_SET_FOCUS);
  }
}
void WxViewPanel::OnKeyDown(wxKeyEvent& event) {
  if (owner_) {
    owner_->OnWxKeyDown(event);
  } else {
    event.Skip();
  }
}
void WxViewPanel::OnKeyUp(wxKeyEvent& event) {
  if (owner_) {
    owner_->OnWxKeyUp(event);
  } else {
    event.Skip();
  }
}
void WxViewPanel::OnKeyChar(wxKeyEvent& event) {
  if (owner_) {
    owner_->OnWxKeyChar(event);
  } else {
    event.Skip();
  }
}
void WxViewPanel::OnMouseDown(wxMouseEvent& event) {
  if (owner_) {
    owner_->OnWxMouseDown(event);
  } else {
    event.Skip();
  }
}
void WxViewPanel::OnMouseUp(wxMouseEvent& event) {
  if (owner_) {
    owner_->OnWxMouseUp(event);
  } else {
    event.Skip();
  }
}
void WxViewPanel::OnMouseMove(wxMouseEvent& event) {
  if (owner_) {
    owner_->OnWxMouseMove(event);
  } else {
    event.Skip();
  }
}
void WxViewPanel::OnMouseWheel(wxMouseEvent& event) {
  if (owner_) {
    owner_->OnWxMouseWheel(event);
  } else {
    event.Skip();
  }
}
void WxViewPanel::OnDpiChanged(wxDPIChangedEvent& event) {
  event.Skip();
  if (owner_) {
    owner_->OnWxDpiChanged(event.GetNewDPI());
  }
}
void WxViewPanel::OnCursorTimer(wxTimerEvent&) {
  if (owner_) {
    owner_->OnWxCursorTimer();
  }
}

WxHostFrame::WxHostFrame(WxWindow* owner, const std::string& title)
    : wxFrame(nullptr, wxID_ANY, WxLabel(title)), owner_(owner) {
  Bind(wxEVT_CLOSE_WINDOW, &WxHostFrame::OnClose, this);
  // Bound once without an ID; menu items are dispatched through the
  // owner's current ID map so menu rebuilds can't leave stale bindings.
  Bind(wxEVT_MENU, &WxHostFrame::OnMenu, this);
  // Backup keyboard path in case the view doesn't have focus (guarded
  // against double handling of events propagated up from the view).
  Bind(wxEVT_KEY_DOWN, &WxHostFrame::OnKeyDown, this);
  Bind(wxEVT_KEY_UP, &WxHostFrame::OnKeyUp, this);
  Bind(wxEVT_CHAR, &WxHostFrame::OnKeyChar, this);
}

void WxHostFrame::DetachOwner() { owner_ = nullptr; }

void WxHostFrame::OnClose(wxCloseEvent& event) {
  if (owner_) {
    owner_->OnWxClose();
  } else {
    event.Skip();
  }
}
void WxHostFrame::OnMenu(wxCommandEvent& event) {
  if (owner_) {
    owner_->OnWxMenu(event.GetId());
  }
}
// Only handle keys that originate on the frame itself; keys from the view
// were already handled there (events propagate upward).
void WxHostFrame::OnKeyDown(wxKeyEvent& event) {
  if (owner_ && event.GetEventObject() == this) {
    owner_->OnWxKeyDown(event);
  } else {
    event.Skip();
  }
}
void WxHostFrame::OnKeyUp(wxKeyEvent& event) {
  if (owner_ && event.GetEventObject() == this) {
    owner_->OnWxKeyUp(event);
  } else {
    event.Skip();
  }
}
void WxHostFrame::OnKeyChar(wxKeyEvent& event) {
  if (owner_ && event.GetEventObject() == this) {
    owner_->OnWxKeyChar(event);
  } else {
    event.Skip();
  }
}

WxDropTarget::WxDropTarget(WxWindow* owner) : owner_(owner) {}

bool WxDropTarget::OnDropFiles(wxCoord, wxCoord,
                               const wxArrayString& filenames) {
  if (owner_ && !filenames.empty()) {
    owner_->OnWxDropFiles(filenames);
    return true;
  }
  return false;
}

WxMenuItem::WxMenuItem(Type type, const std::string& text,
                       const std::string& hotkey,
                       std::function<void()> callback)
    : MenuItem(type, text, hotkey, std::move(callback)) {}

void WxMenuItem::OnChildAdded(MenuItem* child_item) {
  wx_children_.push_back(static_cast<WxMenuItem*>(child_item));
}

void WxMenuItem::OnChildRemoved(MenuItem* child_item) {
  auto it = std::find(wx_children_.begin(), wx_children_.end(),
                      static_cast<WxMenuItem*>(child_item));
  if (it != wx_children_.end()) {
    wx_children_.erase(it);
  }
}

uint32_t WxWindow::GetMediumDpi() const { return 96; }

void WxWindow::RequestCloseImpl() {
  // Let the frame close handler perform the shutdown so listeners run first,
  // like WM_CLOSE / GDK_DELETE do for the native backends.
  if (frame_) {
    frame_->Close(false);
  }
}

void WxWindow::CloseWindowNow() {
  WindowDestructionReceiver destruction_receiver(this);
  OnBeforeClose(destruction_receiver);
  if (destruction_receiver.IsWindowDestroyed()) {
    return;
  }
  auto frame = frame_;
  if (view_) {
    view_->DetachOwner();
  }
  frame_ = nullptr;
  view_ = nullptr;
  // Owned by the frame (directly or through the library book); Destroy takes
  // them down together.
  book_ = nullptr;
  library_view_ = nullptr;
  if (frame) {
    frame->DetachOwner();
    frame->Destroy();
  }
  OnAfterClose();
}

void WxWindow::CompleteOpen() {
  if (!view_) {
    return;
  }
  if (IsMouseCaptureRequested()) {
    view_->CaptureMouse();
  }

  cursor_currently_auto_hidden_ = false;
  CursorVisibility cursor_visibility = GetCursorVisibility();
  if (cursor_visibility != CursorVisibility::kVisible) {
    if (cursor_visibility == CursorVisibility::kAutoHidden) {
      cursor_auto_hide_last_screen_pos_ = wxGetMousePosition();
      cursor_currently_auto_hidden_ = true;
    }
    // OnFocusUpdate needs to be done before this.
    SetCursorIfFocusedOnView(true);
  }

  view_->SetFocus();
}

void WxWindow::ApplyNewFullscreen() {
  if (!frame_) {
    return;
  }
  // Size events from the transition may invoke listeners and destroy us.
  WindowDestructionReceiver destruction_receiver(this);
  frame_->ShowFullScreen(IsFullscreen());
  if (destruction_receiver.IsWindowDestroyedOrClosed()) {
    return;
  }
}

void WxWindow::ApplyNewTitle() {
  if (frame_) {
    frame_->SetTitle(WxLabel(GetTitle()));
  }
}

void WxWindow::RebuildMenuBar() {
  if (!frame_) {
    return;
  }
  menu_items_by_id_.clear();
  const WxMenuItem* root = static_cast<const WxMenuItem*>(GetMainMenu());
  if (!root) {
    frame_->SetMenuBar(nullptr);
    return;
  }
  auto bar = new wxMenuBar();
  for (auto popup : root->wx_children()) {
    if (popup->type() != ui::MenuItem::Type::kPopup) {
      continue;
    }
    wxMenu* menu = BuildPopupMenu(popup);
    bar->Append(menu, WxLabel(popup->text()));
    if (!popup->enabled()) {
      bar->EnableTop(bar->GetMenuCount() - 1, false);
    }
  }
  // Replaces (and deletes) the previous bar.
  frame_->SetMenuBar(bar);
}

wxMenu* WxWindow::BuildPopupMenu(WxMenuItem* popup_item) {
  auto menu = new wxMenu();
  for (auto child : popup_item->wx_children()) {
    switch (child->type()) {
      case ui::MenuItem::Type::kPopup: {
        wxMenu* submenu = BuildPopupMenu(child);
        menu->AppendSubMenu(submenu, WxLabel(child->text()));
        break;
      }
      case ui::MenuItem::Type::kSeparator:
        menu->AppendSeparator();
        break;
      case ui::MenuItem::Type::kString: {
        wxMenuItem* item = menu->Append(wxID_ANY, WxLabel(child->text()));
        if (!child->enabled()) {
          menu->Enable(item->GetId(), false);
        }
        menu_items_by_id_[item->GetId()] = child;
        break;
      }
      case ui::MenuItem::Type::kNormal:
        break;
    }
  }
  return menu;
}

void WxWindow::ApplyNewMainMenu(ui::MenuItem* old_main_menu) {
  (void)old_main_menu;
  // wxWidgets hides the menu bar itself while fullscreen.
  RebuildMenuBar();
}

void WxWindow::CompleteMainMenuItemsUpdateImpl() { RebuildMenuBar(); }

void WxWindow::OnWxMenu(int menu_id) {
  auto it = menu_items_by_id_.find(menu_id);
  if (it == menu_items_by_id_.end()) {
    return;
  }
  WindowDestructionReceiver destruction_receiver(this);
  it->second->OnSelected();
  // The menu item might have been destroyed by its OnSelected.
  (void)destruction_receiver;
}

void WxWindow::ApplyNewMouseCapture() {
  if (!view_ || view_->HasCapture()) {
    return;
  }
  WindowDestructionReceiver destruction_receiver(this);
  view_->CaptureMouse();
  if (destruction_receiver.IsWindowDestroyedOrClosed()) {
    return;
  }
}

void WxWindow::ApplyNewMouseRelease() {
  if (!view_ || !view_->HasCapture()) {
    return;
  }
  WindowDestructionReceiver destruction_receiver(this);
  view_->ReleaseMouse();
  if (destruction_receiver.IsWindowDestroyedOrClosed()) {
    return;
  }
}

void WxWindow::ApplyNewCursorVisibility(
    CursorVisibility old_cursor_visibility) {
  (void)old_cursor_visibility;
  cursor_currently_auto_hidden_ = false;
  if (!view_) {
    return;
  }
  if (GetCursorVisibility() == CursorVisibility::kAutoHidden) {
    cursor_auto_hide_last_screen_pos_ = wxGetMousePosition();
    cursor_currently_auto_hidden_ = true;
    view_->StartCursorTimer();
  } else {
    view_->StopCursorTimer();
  }
  SetCursorIfFocusedOnView(GetCursorVisibility() != CursorVisibility::kVisible);
}

void WxWindow::SetCursorIfFocusedOnView(bool hide_cursor) const {
  if (!view_) {
    return;
  }
  if (!hide_cursor && GetCursorVisibility() == CursorVisibility::kVisible) {
    view_->SetCursor(wxNullCursor);
    return;
  }
  // Hidden or auto-hidden: apply the blank cursor. When not focused the
  // cursor still ends up hidden once the pointer is over the view.
  view_->SetCursor(wxCursor(wxCURSOR_BLANK));
}

void WxWindow::FocusImpl() {
  if (view_) {
    view_->SetFocus();
  }
}

void WxWindow::RequestPaintImpl() {
  if (view_) {
    view_->Refresh(false);
  }
}

void WxWindow::HandleWxSizeUpdate(
    WindowDestructionReceiver& destruction_receiver) {
  uint32_t width = 0, height = 0;
  if (!PlatformClientSize(width, height)) {
    return;
  }
  {
    ui::MonitorUpdateEvent e(this, false);
    OnMonitorUpdate(e);
  }
  if (frame_ && !frame_->IsFullScreen() && !frame_->IsMaximized()) {
    OnDesiredLogicalSizeUpdate(SizeToLogical(width), SizeToLogical(height));
  }
  OnActualSizeUpdate(width, height,
                     frame_ && frame_->IsMaximized()
                         ? WindowResizeAction::kAutoMaximize
                         : WindowResizeAction::kManual,
                     destruction_receiver);
}

void WxWindow::OnWxSize() {
  if (!view_) {
    return;
  }
  WindowDestructionReceiver destruction_receiver(this);
  PlatformDpiRefresh(destruction_receiver);
  if (destruction_receiver.IsWindowDestroyedOrClosed()) {
    return;
  }
  HandleWxSizeUpdate(destruction_receiver);
}

void WxWindow::OnWxPaint() { OnPaint(); }

void WxWindow::OnWxFocus(bool focused) {
  WindowDestructionReceiver destruction_receiver(this);
  OnFocusUpdate(focused, destruction_receiver);
}

void WxWindow::OnWxClose() { CloseWindowNow(); }

void WxWindow::OnWxDropFiles(const wxArrayString& files) {
  if (files.empty()) {
    return;
  }
  ui::FileDropEvent e(this, WxToPath(files[0]));
  WindowDestructionReceiver destruction_receiver(this);
  OnFileDrop(e, destruction_receiver);
}

void WxWindow::OnWxDpiChanged(const wxSize& new_dpi) {
  dpi_ = uint32_t(new_dpi.GetX());
  WindowDestructionReceiver destruction_receiver(this);
  ui::UISetupEvent e(this);
  OnDpiChanged(e, destruction_receiver);
}

void WxWindow::OnWxCursorTimer() {
  if (GetCursorVisibility() == ui::Window::CursorVisibility::kAutoHidden) {
    cursor_currently_auto_hidden_ = true;
    SetCursorIfFocusedOnView(true);
  }
}

void WxWindow::OnWxDeviceChange(bool is_arrival) {
  WindowDestructionReceiver destruction_receiver(this);
  OnUsbDeviceChanged(is_arrival, destruction_receiver);
}

void WxWindow::ForwardWxMouse(wxMouseEvent& event, int kind) {
  if (!view_) {
    event.Skip();
    return;
  }
  // Reveal the cursor on real movement like Win32Window::HandleMouse.
  if (GetCursorVisibility() == CursorVisibility::kAutoHidden) {
    wxPoint screen_pos = view_->ClientToScreen(event.GetPosition());
    if (screen_pos != cursor_auto_hide_last_screen_pos_) {
      cursor_currently_auto_hidden_ = false;
      view_->StartCursorTimer();
      cursor_auto_hide_last_screen_pos_ = screen_pos;
    }
  }

  ui::MouseEvent::Button button = ui::MouseEvent::Button::kNone;
  int32_t scroll_y = 0;
  if (kind == 0 || kind == 1) {
    // Button press/release.
    switch (event.GetButton()) {
      case wxMOUSE_BTN_LEFT:
        button = ui::MouseEvent::Button::kLeft;
        break;
      case wxMOUSE_BTN_RIGHT:
        button = ui::MouseEvent::Button::kRight;
        break;
      case wxMOUSE_BTN_MIDDLE:
        button = ui::MouseEvent::Button::kMiddle;
        break;
      case wxMOUSE_BTN_AUX1:
        button = ui::MouseEvent::Button::kX1;
        break;
      case wxMOUSE_BTN_AUX2:
        button = ui::MouseEvent::Button::kX2;
        break;
      default:
        break;
    }
  } else if (kind == 3) {
    scroll_y = event.GetWheelRotation();
  }

  WindowDestructionReceiver destruction_receiver(this);
  ui::MouseEvent e(this, button, event.GetX(), event.GetY(), 0, scroll_y);
  switch (kind) {
    case 0:
      OnMouseDown(e, destruction_receiver);
      break;
    case 1:
      OnMouseUp(e, destruction_receiver);
      break;
    case 2:
      OnMouseMove(e, destruction_receiver);
      break;
    case 3:
      OnMouseWheel(e, destruction_receiver);
      break;
    default:
      break;
  }
  if (!e.is_handled()) {
    event.Skip();
  }
}

void WxWindow::OnWxMouseDown(wxMouseEvent& event) { ForwardWxMouse(event, 0); }
void WxWindow::OnWxMouseUp(wxMouseEvent& event) { ForwardWxMouse(event, 1); }
void WxWindow::OnWxMouseMove(wxMouseEvent& event) { ForwardWxMouse(event, 2); }
void WxWindow::OnWxMouseWheel(wxMouseEvent& event) { ForwardWxMouse(event, 3); }

bool WxFilePicker::Show(ui::Window* parent_window) {
  wxWindow* parent = nullptr;
  if (auto wx_window = static_cast<WxWindow*>(parent_window)) {
    parent = wx_window->view() ? static_cast<wxWindow*>(wx_window->view())
                               : static_cast<wxWindow*>(wx_window->frame());
  }
  if (type() == Type::kDirectory) {
    wxDirDialog dialog(
        parent, WxLabel(title()),
        file_name().empty() ? wxString() : wxString::FromUTF8(file_name()));
    if (dialog.ShowModal() != wxID_OK) {
      return false;
    }
    set_selected_files({WxToPath(dialog.GetPath())});
    return true;
  }
  wxString wildcard;
  for (const auto& extension : extensions()) {
    if (!wildcard.empty()) {
      wildcard += '|';
    }
    wildcard += WxLabel(extension.first + " (" + extension.second + ")|" +
                        extension.second);
  }
  if (wildcard.empty()) {
    wildcard = "All files (*.*)|*.*";
  }
  long style = mode() == Mode::kSave ? wxFD_SAVE | wxFD_OVERWRITE_PROMPT
                                     : wxFD_OPEN | wxFD_FILE_MUST_EXIST;
  if (multi_selection()) {
    style |= wxFD_MULTIPLE;
  }
  wxFileDialog dialog(parent, WxLabel(title()), wxString(),
                      wxString::FromUTF8(file_name()), wildcard, style);
  if (dialog.ShowModal() != wxID_OK) {
    return false;
  }
  std::vector<std::filesystem::path> selected;
  if (multi_selection()) {
    wxArrayString paths;
    dialog.GetPaths(paths);
    for (const auto& path : paths) {
      selected.push_back(WxToPath(path));
    }
  } else {
    selected.push_back(WxToPath(dialog.GetPath()));
  }
  set_selected_files(std::move(selected));
  return true;
}

void WxWindowedAppContext::NotifyUILoopOfPendingFunctions() {
  // wxApp::CallAfter is thread-safe and dispatches into the wxWidgets event
  // loop, including nested modal loops, like the native loops did.
  if (wxTheApp) {
    wxTheApp->CallAfter([this] { ExecutePendingFunctionsFromUIThread(); });
  }
}

void WxWindowedAppContext::PlatformQuitFromUIThread() {
  if (wxTheApp && wxApp::IsMainLoopRunning()) {
    wxTheApp->ExitMainLoop();
  }
}

void WxWindow::AttachLibrary(LibraryBootCallback on_boot,
                             const std::filesystem::path& storage_root) {
  if (library_view_ || !frame_ || !view_) {
    return;
  }
  library_on_boot_ = std::move(on_boot);
  library_storage_root_ = storage_root;
  LoadLibrary(library_storage_root_, library_entries_);

  book_ = new wxSimplebook(frame_, wxID_ANY);
  frame_->GetSizer()->Detach(view_);
  view_->Reparent(book_);
  book_->AddPage(view_, wxString(), false);
  library_view_ = new WxLibraryView(book_, this, library_storage_root_);
  book_->AddPage(library_view_, wxString(), true);
  library_view_->DragAcceptFiles(true);
  library_view_->SetDropTarget(new WxDropTarget(this));
  frame_->GetSizer()->Add(book_, 1, wxEXPAND);
  frame_->Layout();
  library_view_->SetEntries(library_entries_);
  ShowLibrary();
}

void WxWindow::ShowLibrary() {
  if (!book_ || !library_view_) {
    return;
  }
  book_->ChangeSelection(1);
  frame_->Layout();
  library_view_->SetFocus();
}

void WxWindow::ShowGame() {
  if (!book_ || !view_) {
    return;
  }
  book_->ChangeSelection(0);
  frame_->Layout();
  view_->SetFocus();
}

const GameEntry* WxWindow::LibraryEntry(size_t index) const {
  return index < library_entries_.size() ? &library_entries_[index] : nullptr;
}

void WxWindow::SaveLibraryEntries() {
  if (!library_storage_root_.empty()) {
    SaveLibrary(library_storage_root_, library_entries_);
  }
}

void WxWindow::NoteGameBooted(size_t index, int disc_number) {
  if (index >= library_entries_.size()) {
    return;
  }
  auto& entry = library_entries_[index];
  entry.last_play = std::time(nullptr);
  entry.last_played_disc = disc_number;
  SaveLibraryEntries();
  if (library_view_) {
    library_view_->RefreshEntry(index);
  }
}

void WxWindow::ImportLibraryPaths(
    const std::vector<std::filesystem::path>& paths) {
  if (!library_view_ || paths.empty()) {
    return;
  }
  if (ImportGamePaths(library_view_, library_storage_root_, library_entries_,
                      paths)) {
    SaveLibraryEntries();
    library_view_->SetEntries(library_entries_);
  }
}

void WxWindow::ScanLibraryFolder(const std::filesystem::path& dir) {
  ImportLibraryPaths(DiscoverGameFiles(dir));
}

void WxWindow::RemoveLibraryEntry(size_t index) {
  if (index >= library_entries_.size() || !library_view_) {
    return;
  }
  const auto& entry = library_entries_[index];
  if (wxMessageBox(WxLabel("Remove \"" + entry.name +
                           "\" from the library?\n"
                           "Game files on disk are kept."),
                   "Remove game", wxYES_NO | wxICON_QUESTION,
                   library_view_) != wxYES) {
    return;
  }
  std::error_code ec = {};
  std::filesystem::remove_all(ArtworkDir(library_storage_root_, entry.title_id),
                              ec);
  library_entries_.erase(library_entries_.begin() + index);
  SaveLibraryEntries();
  library_view_->SetEntries(library_entries_);
}

void WxWindow::OnBootGame(size_t index, int disc_number) {
  if (!library_on_boot_ || index >= library_entries_.size() ||
      disc_number <= 0) {
    return;
  }
  const auto* path = library_entries_[index].DiscPath(disc_number);
  if (path && !path->empty()) {
    library_on_boot_(index, disc_number, *path);
  }
}

void WxWindow::OnRemoveGame(size_t index) { RemoveLibraryEntry(index); }

void WxWindow::OnShowInFolder(size_t index) {
  const GameEntry* entry = LibraryEntry(index);
  if (!entry || entry->discs.empty()) {
    return;
  }
#if defined(__WXMSW__)
  wxExecute("explorer /select,\"" +
            WxLabel(xe::path_to_utf8(entry->discs[0].path)) + "\"");
#else
  std::filesystem::path dir = entry->discs[0].path;
  std::error_code ec = {};
  if (!std::filesystem::is_directory(dir, ec)) {
    dir = dir.parent_path();
  }
  wxExecute("xdg-open \"" + WxLabel(xe::path_to_utf8(dir)) + "\"");
#endif
}

void WxWindow::OnAddGame() {
  if (!library_view_) {
    return;
  }
  wxFileDialog dialog(
      library_view_, "Add Game", wxString(), wxString(),
      "Xbox 360 games (*.xex;*.iso;*.xiso;*.zar)|*.xex;*.iso;*.xiso;*.zar|"
      "All files (*.*)|*.*",
      wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_MULTIPLE);
  if (dialog.ShowModal() != wxID_OK) {
    return;
  }
  wxArrayString wx_paths;
  dialog.GetPaths(wx_paths);
  std::vector<std::filesystem::path> paths;
  for (const auto& p : wx_paths) {
    paths.push_back(WxToPath(p));
  }
  ImportLibraryPaths(paths);
}

void WxWindow::OnScanFolder() {
  if (!library_view_) {
    return;
  }
  wxDirDialog dialog(library_view_, "Scan Folder for Games");
  if (dialog.ShowModal() != wxID_OK) {
    return;
  }
  ScanLibraryFolder(WxToPath(dialog.GetPath()));
}

}  // namespace wx_ui
}  // namespace app
}  // namespace xe

namespace xe {
namespace ui {

std::unique_ptr<Window> Window::Create(WindowedAppContext& app_context,
                                       const std::string_view title,
                                       uint32_t desired_logical_width,
                                       uint32_t desired_logical_height) {
  return std::make_unique<app::wx_ui::WxWindow>(
      app_context, title, desired_logical_width, desired_logical_height);
}

std::unique_ptr<MenuItem> MenuItem::Create(MenuItem::Type type,
                                           const std::string& text,
                                           const std::string& hotkey,
                                           std::function<void()> callback) {
  return std::make_unique<app::wx_ui::WxMenuItem>(type, text, hotkey,
                                                  std::move(callback));
}

std::unique_ptr<FilePicker> FilePicker::Create() {
  return std::make_unique<app::wx_ui::WxFilePicker>();
}

}  // namespace ui
}  // namespace xe
