/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_WX_CONSOLE_SETTINGS_DIALOG_H_
#define XENIA_APP_WX_CONSOLE_SETTINGS_DIALOG_H_

class wxWindow;

namespace xe {
namespace kernel {
class KernelState;
}  // namespace kernel
namespace app {
namespace wx_ui {

class WxWindow;

// Modal wxWidgets port of ConsoleSettingsDialog. Edits a staged XConfigData
// copy; Save writes it live and flushes to disk. Runs on the UI thread.
void ShowConsoleSettingsDialog(WxWindow* window,
                               kernel::KernelState* kernel_state);

}  // namespace wx_ui
}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_WX_CONSOLE_SETTINGS_DIALOG_H_
