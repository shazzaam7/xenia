/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_WX_CONFIG_EDITOR_DIALOG_H_
#define XENIA_APP_WX_CONFIG_EDITOR_DIALOG_H_

namespace xe {
namespace app {
namespace wx_ui {

class WxWindow;

// Modal config editor. One notebook tab per cvar category (matching the
// xenia-canary.config.toml sections); Save validates, applies live and writes
// the file. Runs on the UI thread.
void ShowConfigEditorDialog(WxWindow* window);

}  // namespace wx_ui
}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_WX_CONFIG_EDITOR_DIALOG_H_
