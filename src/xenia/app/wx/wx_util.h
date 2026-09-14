#ifndef XENIA_APP_WX_UTIL_H_
#define XENIA_APP_WX_UTIL_H_

#include <filesystem>
#include <string>

#include <wx/string.h>

#include "xenia/base/filesystem.h"

namespace xe {
namespace app {
namespace wx_ui {

// Menu/file dialog labels. Note: hotkey suffixes are intentionally not
// appended with \t - wxWidgets would turn those into real accelerators that
// swallow key events before EmulatorWindow::OnKeyDown and the keyboard HID
// driver see them. Menu entries and hotkeys share the same callbacks anyway.
inline wxString WxLabel(const std::string& text) {
  return wxString::FromUTF8(text);
}

inline std::filesystem::path WxToPath(const wxString& string) {
  return xe::to_path(
      std::u16string_view(reinterpret_cast<const char16_t*>(string.wc_str())));
}

}  // namespace wx_ui
}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_WX_UTIL_H_
