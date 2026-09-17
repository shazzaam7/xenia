/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_WX_CONTENT_INSTALL_DIALOG_H_
#define XENIA_APP_WX_CONTENT_INSTALL_DIALOG_H_

#include <filesystem>
#include <memory>
#include <vector>

#include "xenia/emulator.h"

namespace xe {
namespace app {
namespace wx_ui {

class WxWindow;

// Modeless wxWidgets replacement for EmulatorWindow::ContentInstallDialog.
// The ImGui dialog is hidden behind the library view when the library is
// attached, so install/extract progress needs a native window instead.
// Polls the shared entries on a UI-thread timer; the worker thread never
// touches wx. Safe to call with entries shared with a running install.
void ShowContentInstallDialog(
    WxWindow* window,
    std::shared_ptr<std::vector<Emulator::ContentInstallEntry>> entries,
    const std::filesystem::path& content_root, bool is_extract);

}  // namespace wx_ui
}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_WX_CONTENT_INSTALL_DIALOG_H_
