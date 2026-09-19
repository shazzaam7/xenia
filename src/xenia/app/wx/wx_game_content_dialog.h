/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_WX_GAME_CONTENT_DIALOG_H_
#define XENIA_APP_WX_GAME_CONTENT_DIALOG_H_

#include <cstdint>
#include <filesystem>
#include <string>

class wxWindow;

namespace xe {
namespace kernel {
class KernelState;
}  // namespace kernel
namespace app {
namespace wx_ui {

// Modal per-game content dialog (Achievements / Saves / Title Updates /
// Marketplace notebook), opened from the library context menu.
void ShowGameContentDialog(wxWindow* parent, kernel::KernelState* kernel_state,
                           const std::filesystem::path& content_root,
                           uint32_t title_id, const std::string& title_name);

}  // namespace wx_ui
}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_WX_GAME_CONTENT_DIALOG_H_
