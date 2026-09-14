/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// Windows-specific parts of the wxWidgets backend (HWND surface, .ico icons,
// USB arrival notifications, DPI handling). Shared plumbing in wx_window.cc.

#include "xenia/app/wx/wx_window.h"

#include "xenia/app/wx/wx_util.h"
#include "xenia/app/wx/wx_window_priv.h"

#include <Dbt.h>
#include <dwmapi.h>

#include <wx/event.h>
#include <wx/frame.h>
#include <wx/menu.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/utils.h>

#include "xenia/ui/surface_win.h"
#include "xenia/ui/virtual_key.h"

namespace xe {
namespace app {
namespace wx_ui {

namespace {

// GetDpiForWindow is Windows 10 1607+; load dynamically like the Win32
// backend does so the import table doesn't require it.
uint32_t GetHwndDpi(HWND hwnd) {
  static decltype(&GetDpiForWindow) get_dpi_for_window =
      reinterpret_cast<decltype(&GetDpiForWindow)>(
          GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
  if (hwnd && get_dpi_for_window) {
    UINT dpi = get_dpi_for_window(hwnd);
    if (dpi) {
      return dpi;
    }
  }
  return 0;
}

}  // namespace

WxWindowedAppContext::WxWindowedAppContext(HINSTANCE hinstance, int show_cmd)
    : hinstance_(hinstance), show_cmd_(show_cmd) {}

WxWindow::WxWindow(ui::WindowedAppContext& app_context,
                   const std::string_view title, uint32_t desired_logical_width,
                   uint32_t desired_logical_height)
    : Window(app_context, title, desired_logical_width,
             desired_logical_height) {
  // Closest approximation before a native window exists, like
  // Win32Window::Win32Window.
  HDC screen_hdc = GetDC(nullptr);
  if (screen_hdc) {
    dpi_ = uint32_t(GetDeviceCaps(screen_hdc, LOGPIXELSX));
    ReleaseDC(nullptr, screen_hdc);
  }
}

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
    view_hwnd_ = nullptr;
    // The message loop is already gone at this point (this runs after quit),
    // so this only releases the native window; the process exits right after.
    frame->Destroy();
  }
  if (usb_device_notify_) {
    UnregisterDeviceNotification(usb_device_notify_);
    usb_device_notify_ = nullptr;
  }
  if (icon_) {
    DestroyIcon(icon_);
    icon_ = nullptr;
  }
}

uint32_t WxWindow::GetLatestDpiImpl() const {
  uint32_t dpi = GetHwndDpi(view_hwnd_);
  if (dpi) {
    return dpi;
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
  view_hwnd_ = static_cast<HWND>(view_->GetHWND());

  view_->DragAcceptFiles(true);
  view_->SetDropTarget(new WxDropTarget(this));

  DEV_BROADCAST_DEVICEINTERFACE filter = {};
  filter.dbcc_size = sizeof(filter);
  filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
  usb_device_notify_ = RegisterDeviceNotificationW(
      view_hwnd_, &filter,
      DEVICE_NOTIFY_WINDOW_HANDLE | DEVICE_NOTIFY_ALL_INTERFACE_CLASSES);

  // Default application icon (MAINICON resource), like the Win32 backend's
  // window class icon. Shared resource icon; never destroyed.
  HINSTANCE hinstance =
      static_cast<const WxWindowedAppContext&>(app_context()).hinstance();
  default_icon_ = LoadIconW(hinstance, L"MAINICON");
  ApplyFrameIcons();

  HWND frame_hwnd = static_cast<HWND>(frame_->GetHWND());
  DWM_WINDOW_CORNER_PREFERENCE corner_preference = DWMWCP_DONOTROUND;
  DwmSetWindowAttribute(frame_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE,
                        &corner_preference, sizeof(corner_preference));

  if (GetMainMenu()) {
    RebuildMenuBar();
  }

  // Size the frame so the view client area matches the desired logical size.
  dpi_ = GetLatestDpiImpl();
  RECT rect = {
      0, 0,
      LONG(ConvertSizeDpi(GetDesiredLogicalWidth(), dpi_, GetMediumDpi())),
      LONG(ConvertSizeDpi(GetDesiredLogicalHeight(), dpi_, GetMediumDpi()))};
  DWORD style = DWORD(GetWindowLong(frame_hwnd, GWL_STYLE));
  DWORD ex_style = DWORD(GetWindowLong(frame_hwnd, GWL_EXSTYLE));
  static decltype(&AdjustWindowRectExForDpi) adjust_for_dpi =
      reinterpret_cast<decltype(&AdjustWindowRectExForDpi)>(GetProcAddress(
          GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi"));
  if (adjust_for_dpi) {
    adjust_for_dpi(&rect, style, GetMainMenu() != nullptr, ex_style, dpi_);
  } else {
    AdjustWindowRectEx(&rect, style, GetMainMenu() != nullptr, ex_style);
  }
  SetWindowPos(frame_hwnd, nullptr, 0, 0, rect.right - rect.left,
               rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER);

  if (IsFullscreen()) {
    // Go fullscreen after setting up the windowed placement.
    WindowDestructionReceiver destruction_receiver(this);
    frame_->ShowFullScreen(true);
    if (destruction_receiver.IsWindowDestroyed()) {
      return true;
    }
  }

  frame_->Show(true);

  // Report the initial actual state after opening.
  {
    WindowDestructionReceiver destruction_receiver(this);
    OnDesiredLogicalSizeUpdate(GetDesiredLogicalWidth(),
                               GetDesiredLogicalHeight());
    RECT client_rect = {};
    if (GetClientRect(view_hwnd_, &client_rect)) {
      OnActualSizeUpdate(uint32_t(client_rect.right),
                         uint32_t(client_rect.bottom),
                         WindowResizeAction::kManual, destruction_receiver);
      if (destruction_receiver.IsWindowDestroyedOrClosed()) {
        return true;
      }
    }
    OnFocusUpdate(::GetFocus() == view_hwnd_, destruction_receiver);
    if (destruction_receiver.IsWindowDestroyedOrClosed()) {
      return true;
    }
  }

  CompleteOpen();
  return true;
}

bool WxWindow::PlatformClientSize(uint32_t& width_out, uint32_t& height_out) {
  if (!view_hwnd_) {
    return false;
  }
  RECT client_rect = {};
  if (!GetClientRect(view_hwnd_, &client_rect)) {
    return false;
  }
  width_out = uint32_t(client_rect.right);
  height_out = uint32_t(client_rect.bottom);
  return true;
}

void WxWindow::PlatformDpiRefresh(WindowDestructionReceiver&) {
  // DPI changes arrive via wxDPIChangedEvent on Windows.
}

void WxWindow::ApplyFrameIcons() {
  if (!frame_) {
    return;
  }
  HICON icon = icon_ ? icon_ : default_icon_;
  if (!icon) {
    return;
  }
  HWND frame_hwnd = static_cast<HWND>(frame_->GetHWND());
  SendMessageW(frame_hwnd, WM_SETICON, ICON_BIG,
               reinterpret_cast<LPARAM>(icon));
  SendMessageW(frame_hwnd, WM_SETICON, ICON_SMALL,
               reinterpret_cast<LPARAM>(icon));
}

void WxWindow::LoadAndApplyIcon(const void* buffer, size_t size,
                                bool can_apply_state_in_current_phase) {
  // Same handling as Win32Window::LoadAndApplyIcon; the buffer is in Windows
  // .ico format. Falls back to the default MAINICON resource icon.
  bool reset = !buffer || !size;
  if (reset && !icon_) {
    // Already showing the default icon.
    return;
  }
  HICON new_icon = nullptr;
  if (!reset) {
    new_icon = CreateIconFromResourceEx(
        static_cast<PBYTE>(const_cast<void*>(buffer)), DWORD(size), true,
        0x00030000, 0, 0, LR_DEFAULTCOLOR | LR_DEFAULTSIZE);
    if (!new_icon) {
      return;
    }
  }
  // The old custom icon is not in use anymore, safe to destroy it now (the
  // default icon is shared and must never be destroyed).
  if (icon_) {
    DestroyIcon(icon_);
    icon_ = nullptr;
  }
  if (!reset) {
    icon_ = new_icon;
  }
  if (can_apply_state_in_current_phase) {
    ApplyFrameIcons();
  }
}

std::unique_ptr<ui::Surface> WxWindow::CreateSurfaceImpl(
    ui::Surface::TypeFlags allowed_types) {
  HINSTANCE hinstance =
      static_cast<const WxWindowedAppContext&>(app_context()).hinstance();
  if ((allowed_types & ui::Surface::kTypeFlag_Win32Hwnd) && view_hwnd_) {
    return std::make_unique<ui::Win32HwndSurface>(hinstance, view_hwnd_);
  }
  return nullptr;
}

void WxWindow::OnWxKeyDown(wxKeyEvent& event) {
  if (!view_) {
    event.Skip();
    return;
  }
  WindowDestructionReceiver destruction_receiver(this);
  ui::KeyEvent e(this, ui::VirtualKey(event.GetRawKeyCode()), 1, false,
                 (event.GetModifiers() & wxMOD_SHIFT) != 0,
                 (event.GetModifiers() & wxMOD_CONTROL) != 0,
                 (event.GetModifiers() & wxMOD_ALT) != 0,
                 wxGetKeyState(WXK_CAPITAL));
  OnKeyDown(e, destruction_receiver);
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
  ui::KeyEvent e(this, ui::VirtualKey(event.GetRawKeyCode()), 1, false,
                 (event.GetModifiers() & wxMOD_SHIFT) != 0,
                 (event.GetModifiers() & wxMOD_CONTROL) != 0,
                 (event.GetModifiers() & wxMOD_ALT) != 0,
                 wxGetKeyState(WXK_CAPITAL));
  OnKeyUp(e, destruction_receiver);
  if (!e.is_handled()) {
    event.Skip();
  }
}

void WxWindow::OnWxKeyChar(wxKeyEvent& event) {
  if (!view_) {
    event.Skip();
    return;
  }
  int unicode = event.GetUnicodeKey();
  if (unicode == WXK_NONE) {
    event.Skip();
    return;
  }
  WindowDestructionReceiver destruction_receiver(this);
  ui::KeyEvent e(this, ui::VirtualKey::kNone, 1, false,
                 (event.GetModifiers() & wxMOD_SHIFT) != 0,
                 (event.GetModifiers() & wxMOD_CONTROL) != 0,
                 (event.GetModifiers() & wxMOD_ALT) != 0,
                 wxGetKeyState(WXK_CAPITAL));
  // Guarantee input driver receives the unicode corresponding to its virtual
  // key.
  e.set_unicode(static_cast<uint16_t>(unicode));
  int16_t vk_result = VkKeyScanW(static_cast<WCHAR>(unicode));
  if (vk_result >= 0) {
    e.set_virtual_key(ui::VirtualKey(LOBYTE(vk_result)));
  }
  OnKeyChar(e, destruction_receiver);
  if (!e.is_handled()) {
    event.Skip();
  }
}

}  // namespace wx_ui
}  // namespace app
}  // namespace xe
