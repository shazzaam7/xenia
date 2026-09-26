#ifndef XENIA_APP_WX_UTIL_H_
#define XENIA_APP_WX_UTIL_H_

#include <filesystem>
#include <string>
#include <string_view>

#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/string.h>
#include <wx/window.h>

#include "xenia/base/filesystem.h"

namespace xe {
namespace app {
namespace wx_ui {

// Scales raw pixels to the monitor a window is on. Thin named wrapper over
// wxWindow::FromDIP so size-defining call sites read in DIPs. All fixed
// widget/icon/column/dialog sizes must go through this (or FromDIP
// directly); sizer code otherwise stays in 96-DPI pixels forever.
inline int DipPx(wxWindow* window, int px) {
  return window ? window->FromDIP(px) : px;
}
inline wxSize DipSize(wxWindow* window, int w, int h) {
  return wxSize(DipPx(window, w), DipPx(window, h));
}

// Menu/file dialog labels. Note: hotkey suffixes are intentionally not
// appended with \t - wxWidgets would turn those into real accelerators that
// swallow key events before EmulatorWindow::OnKeyDown and the keyboard HID
// driver see them. Menu entries and hotkeys share the same callbacks anyway.
inline wxString WxLabel(const std::string& text) {
  return wxString::FromUTF8(text);
}

inline std::filesystem::path WxToPath(const wxString& string) {
  // Must not go through wc_str(): that is wchar_t*, which is 2 bytes on
  // Windows but 4 on POSIX, so reinterpreting it as char16_t and letting
  // u16string_view stop at the first NUL truncated every path to its first
  // character on Linux ("/home/goose/...iso" became "/", and the library
  // scanner then walked the entire filesystem). The UTF-8 buffer is what
  // std::filesystem wants on every platform.
  return xe::to_path(std::string_view(string.ToUTF8()));
}

// True when the OS window background is dark (i.e. a dark theme is active).
// Used for theme-dependent custom colors; system colors need no check.
inline bool IsDarkTheme() {
  const wxColour bg = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
  return (0.299 * bg.Red() + 0.587 * bg.Green() + 0.114 * bg.Blue()) < 128.0;
}

// Validation-error field background: readable on both light and dark themes.
inline wxColour ErrorBgColour() {
  return IsDarkTheme() ? wxColour(0x66, 0x1A, 0x1A) : wxColour(255, 180, 180);
}

}  // namespace wx_ui
}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_WX_UTIL_H_
