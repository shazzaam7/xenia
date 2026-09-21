/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_WX_GAME_CONFIG_DIALOG_H_
#define XENIA_APP_WX_GAME_CONFIG_DIALOG_H_

#include <string>

namespace xe {
namespace app {
namespace wx_ui {

class WxWindow;

// Modal editor for one title's sparse override file
// (config/<TITLEID>.config.toml), reachable from the game library's context
// menu. Every row pairs an override control with a "Use global" button (its
// tooltip shows the inherited value), and only rows that differ from the
// global value are written - clearing the last override deletes the file,
// which is what makes a title fall back to the global config entirely.
void ShowGameConfigDialog(WxWindow* window, const std::string& title_id,
                          const std::string& title_name);

}  // namespace wx_ui
}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_WX_GAME_CONFIG_DIALOG_H_
