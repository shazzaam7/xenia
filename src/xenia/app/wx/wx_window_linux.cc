/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// Linux-specific parts of the wxWidgets backend (XCB surface, WXK key map).
// The process forces GDK_BACKEND=x11 like the GTK backend does; there is no
// Wayland surface type. Shared plumbing in wx_window.cc.

#include "xenia/app/wx/wx_window.h"

#include "xenia/app/wx/wx_util.h"
#include "xenia/app/wx/wx_window_priv.h"

#include <gdk/gdkx.h>
#include <glib.h>
#include <gtk/gtk.h>

#include <X11/Xlib-xcb.h>
#include <xcb/xcb.h>

#include <wx/event.h>
#include <wx/frame.h>
#include <wx/menu.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/utils.h>

#include "xenia/base/logging.h"
#include "xenia/ui/surface_gnulinux.h"
#include "xenia/ui/virtual_key.h"

namespace xe {
namespace app {
namespace wx_ui {

namespace {

// Translates a portable wxWidgets key code to xe::ui::VirtualKey. ASCII
// letters, digits and punctuation match the Xbox virtual keys; the rest needs
// an explicit table (mirrors GTKWindow::TranslateVirtualKeyToUKLayout for the
// keys wxWidgets can report).
ui::VirtualKey TranslateWxKey(int key_code) {
  using ui::VirtualKey;
  if ((key_code >= '0' && key_code <= '9') ||
      (key_code >= 'A' && key_code <= 'Z') ||
      (key_code >= 'a' && key_code <= 'z')) {
    return VirtualKey(key_code);
  }
  switch (key_code) {
    case WXK_BACK:
      return VirtualKey::kBack;
    case WXK_TAB:
      return VirtualKey::kTab;
    case WXK_RETURN:
    case WXK_NUMPAD_ENTER:
      return VirtualKey::kReturn;
    case WXK_ESCAPE:
      return VirtualKey::kEscape;
    case WXK_SPACE:
    case WXK_NUMPAD_SPACE:
      return VirtualKey::kSpace;
    case WXK_DELETE:
    case WXK_NUMPAD_DELETE:
      return VirtualKey::kDelete;
    case WXK_LBUTTON:
      return VirtualKey::kLButton;
    case WXK_RBUTTON:
      return VirtualKey::kRButton;
    case WXK_CANCEL:
      return VirtualKey::kCancel;
    case WXK_MBUTTON:
      return VirtualKey::kMButton;
    case WXK_CLEAR:
    case WXK_NUMPAD_BEGIN:
      return VirtualKey::kClear;
    case WXK_SHIFT:
      return VirtualKey::kShift;
    case WXK_ALT:
    case WXK_MENU:
      return VirtualKey::kMenu;
    case WXK_CONTROL:
      return VirtualKey::kControl;
    case WXK_PAUSE:
      return VirtualKey::kPause;
    case WXK_CAPITAL:
      return VirtualKey::kCapital;
    case WXK_END:
    case WXK_NUMPAD_END:
      return VirtualKey::kEnd;
    case WXK_HOME:
    case WXK_NUMPAD_HOME:
      return VirtualKey::kHome;
    case WXK_LEFT:
    case WXK_NUMPAD_LEFT:
      return VirtualKey::kLeft;
    case WXK_UP:
    case WXK_NUMPAD_UP:
      return VirtualKey::kUp;
    case WXK_RIGHT:
    case WXK_NUMPAD_RIGHT:
      return VirtualKey::kRight;
    case WXK_DOWN:
    case WXK_NUMPAD_DOWN:
      return VirtualKey::kDown;
    case WXK_SELECT:
      return VirtualKey::kSelect;
    case WXK_PRINT:
      return VirtualKey::kPrint;
    case WXK_EXECUTE:
      return VirtualKey::kExecute;
    case WXK_SNAPSHOT:
      return VirtualKey::kSnapshot;
    case WXK_INSERT:
    case WXK_NUMPAD_INSERT:
      return VirtualKey::kInsert;
    case WXK_HELP:
      return VirtualKey::kHelp;
    case WXK_NUMPAD0:
      return VirtualKey::kNumpad0;
    case WXK_NUMPAD1:
      return VirtualKey::kNumpad1;
    case WXK_NUMPAD2:
      return VirtualKey::kNumpad2;
    case WXK_NUMPAD3:
      return VirtualKey::kNumpad3;
    case WXK_NUMPAD4:
      return VirtualKey::kNumpad4;
    case WXK_NUMPAD5:
      return VirtualKey::kNumpad5;
    case WXK_NUMPAD6:
      return VirtualKey::kNumpad6;
    case WXK_NUMPAD7:
      return VirtualKey::kNumpad7;
    case WXK_NUMPAD8:
      return VirtualKey::kNumpad8;
    case WXK_NUMPAD9:
      return VirtualKey::kNumpad9;
    case WXK_MULTIPLY:
      return VirtualKey::kMultiply;
    case WXK_ADD:
      return VirtualKey::kAdd;
    case WXK_SEPARATOR:
      return VirtualKey::kSeparator;
    case WXK_SUBTRACT:
      return VirtualKey::kSubtract;
    case WXK_DECIMAL:
      return VirtualKey::kDecimal;
    case WXK_DIVIDE:
      return VirtualKey::kDivide;
    case WXK_NUMLOCK:
      return VirtualKey::kNumLock;
    case WXK_SCROLL:
      return VirtualKey::kScroll;
    case WXK_PAGEUP:
    case WXK_NUMPAD_PAGEUP:
      return VirtualKey::kPrior;
    case WXK_PAGEDOWN:
    case WXK_NUMPAD_PAGEDOWN:
      return VirtualKey::kNext;
    case WXK_NUMPAD_TAB:
      return VirtualKey::kTab;
    default:
      break;
  }
  if (key_code >= WXK_F1 && key_code <= WXK_F24) {
    return VirtualKey(int(VirtualKey::kF1) + (key_code - WXK_F1));
  }
  return VirtualKey::kNone;
}

}  // namespace

WxWindow::WxWindow(ui::WindowedAppContext& app_context,
                   const std::string_view title, uint32_t desired_logical_width,
                   uint32_t desired_logical_height)
    : Window(app_context, title, desired_logical_width,
             desired_logical_height) {}

WxWindow::~WxWindow() {
  EnterDestructor();
  if (view_) {
    view_->DetachOwner();
  }
  if (frame_) {
    auto frame = frame_;
    frame->DetachOwner();
    frame_ = nullptr;
    view_ = nullptr;
    view_xcb_connection_ = nullptr;
    view_xid_ = 0;
    // The message loop is already gone at this point (this runs after quit),
    // so this only releases the native window; the process exits right after.
    frame->Destroy();
  }
}

uint32_t WxWindow::GetLatestDpiImpl() const {
  if (view_) {
    int dpi = view_->GetDPI().GetX();
    if (dpi > 0) {
      return uint32_t(dpi);
    }
  }
  return dpi_ ? dpi_ : GetMediumDpi();
}

bool WxWindow::OpenImpl() {
  frame_ = new WxHostFrame(this, GetTitle());
  view_ = new WxViewPanel(this, frame_);
  auto sizer = new wxBoxSizer(wxVERTICAL);
  sizer->Add(view_, 1, wxEXPAND);
  frame_->SetSizer(sizer);
  frame_->Layout();

  view_->DragAcceptFiles(true);
  view_->SetDropTarget(new WxDropTarget(this));

  if (GetMainMenu()) {
    RebuildMenuBar();
  }

  // Like GTKWindow, let the toolkit size the window around the desired client
  // area (the desired size is updated from the actual layout afterwards).
  frame_->SetClientSize(
      wxSize(int(GetDesiredLogicalWidth()), int(GetDesiredLogicalHeight())));
  frame_->Show(true);

  // The widget must be realized before its native window exists (a no-op if
  // showing above already realized it). Presenting needs X11 (same
  // requirement as the GTK backend); there is no Wayland surface type.
  GtkWidget* gtk_widget = static_cast<GtkWidget*>(view_->GetHandle());
  if (gtk_widget) {
    gtk_widget_realize(gtk_widget);
  }
  GdkWindow* gdk_window =
      gtk_widget ? gtk_widget_get_window(gtk_widget) : nullptr;
  GdkDisplay* gdk_display =
      gdk_window ? gdk_window_get_display(gdk_window) : nullptr;
  if (!gdk_window || !gdk_display || !GDK_IS_X11_DISPLAY(gdk_display)) {
    XELOGE("WxWindow: An X11 window is required for presenting");
    return false;
  }
  view_xcb_connection_ =
      XGetXCBConnection(gdk_x11_display_get_xdisplay(gdk_display));
  view_xid_ = gdk_x11_window_get_xid(gdk_window);
  if (!view_xcb_connection_ || !view_xid_) {
    XELOGE("WxWindow: Failed to obtain the native X11 window");
    return false;
  }

  dpi_ = GetLatestDpiImpl();

  if (IsFullscreen()) {
    // Go fullscreen after setting up the windowed placement.
    WindowDestructionReceiver destruction_receiver(this);
    frame_->ShowFullScreen(true);
    if (destruction_receiver.IsWindowDestroyed()) {
      return true;
    }
  }

  // Report the initial actual state after opening.
  {
    WindowDestructionReceiver destruction_receiver(this);
    OnDesiredLogicalSizeUpdate(GetDesiredLogicalWidth(),
                               GetDesiredLogicalHeight());
    uint32_t width = 0, height = 0;
    if (PlatformClientSize(width, height)) {
      OnActualSizeUpdate(width, height, WindowResizeAction::kManual,
                         destruction_receiver);
      if (destruction_receiver.IsWindowDestroyedOrClosed()) {
        return true;
      }
    }
    OnFocusUpdate(view_->HasFocus(), destruction_receiver);
    if (destruction_receiver.IsWindowDestroyedOrClosed()) {
      return true;
    }
  }

  CompleteOpen();
  return true;
}

bool WxWindow::PlatformClientSize(uint32_t& width_out, uint32_t& height_out) {
  if (!view_) {
    return false;
  }
  wxSize size = view_->GetClientSize();
  double scale = view_->GetDPIScaleFactor();
  if (!(scale > 0.0)) {
    scale = 1.0;
  }
  width_out = uint32_t(size.x * scale);
  height_out = uint32_t(size.y * scale);
  return true;
}

void WxWindow::PlatformDpiRefresh(
    WindowDestructionReceiver& destruction_receiver) {
  // wxGTK has no reliable DPI-change notification; re-query on resize.
  if (!view_) {
    return;
  }
  int dpi = view_->GetDPI().GetX();
  if (dpi > 0 && uint32_t(dpi) != dpi_) {
    dpi_ = uint32_t(dpi);
    ui::UISetupEvent e(this);
    OnDpiChanged(e, destruction_receiver);
  }
}

void WxWindow::LoadAndApplyIcon(const void*, size_t,
                                bool can_apply_state_in_current_phase) {
  // No icon handling on Linux, same as the GTK backend.
  (void)can_apply_state_in_current_phase;
}

std::unique_ptr<ui::Surface> WxWindow::CreateSurfaceImpl(
    ui::Surface::TypeFlags allowed_types) {
  if ((allowed_types & ui::Surface::kTypeFlag_XcbWindow) &&
      view_xcb_connection_ && view_xid_) {
    return std::make_unique<ui::XcbWindowSurface>(
        static_cast<xcb_connection_t*>(view_xcb_connection_), view_xid_);
  }
  return nullptr;
}

void WxWindow::OnWxKeyDown(wxKeyEvent& event) {
  if (!view_) {
    event.Skip();
    return;
  }
  WindowDestructionReceiver destruction_receiver(this);
  int modifiers = event.GetModifiers();
  // The GTK backend reported GDK_META_MASK as Alt; physical Alt is MOD1.
  // Accept either so Alt works regardless of XKB layout.
  bool alt_pressed = (modifiers & (wxMOD_ALT | wxMOD_META)) != 0;
  ui::KeyEvent e(this, TranslateWxKey(event.GetKeyCode()), 1, false,
                 (modifiers & wxMOD_SHIFT) != 0,
                 (modifiers & wxMOD_CONTROL) != 0, alt_pressed,
                 wxGetKeyState(WXK_CAPITAL));
  // Handle OnKeyDown before OnKeyChar so the input driver can update the
  // unicode for the key press (same as the GTK backend).
  OnKeyDown(e, destruction_receiver);
  int unicode = event.GetUnicodeKey();
  if (unicode > 0 && g_unichar_isprint(unicode)) {
    e.set_unicode(uint16_t(unicode));
    OnKeyChar(e, destruction_receiver);
  }
  if (!e.is_handled()) {
    event.Skip();
  }
}

void WxWindow::OnWxKeyUp(wxKeyEvent& event) {
  if (!view_) {
    event.Skip();
    return;
  }
  WindowDestructionReceiver destruction_receiver(this);
  int modifiers = event.GetModifiers();
  bool alt_pressed = (modifiers & (wxMOD_ALT | wxMOD_META)) != 0;
  ui::KeyEvent e(this, TranslateWxKey(event.GetKeyCode()), 1, true,
                 (modifiers & wxMOD_SHIFT) != 0,
                 (modifiers & wxMOD_CONTROL) != 0, alt_pressed,
                 wxGetKeyState(WXK_CAPITAL));
  OnKeyUp(e, destruction_receiver);
  if (!e.is_handled()) {
    event.Skip();
  }
}

void WxWindow::OnWxKeyChar(wxKeyEvent& event) {
  // Produced from the key press above (GTK backend parity); nothing to do.
  if (!view_) {
    event.Skip();
    return;
  }
  event.Skip();
}

}  // namespace wx_ui
}  // namespace app
}  // namespace xe
