/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_WX_PATCH_DIALOG_H_
#define XENIA_APP_WX_PATCH_DIALOG_H_

#include <filesystem>
#include <string>

class wxWindow;

namespace xe {
namespace app {
namespace wx_ui {

// Modal per-title patch editor, opened from the game library's context menu.
// One treebook page per installed *.patch.toml file for the title, each with
// a checkbox per [[patch]] entry bound to its is_enabled flag, plus a search
// box filtering entries across files. Saving rewrites only the changed files
// (with a .bak kept next to each). Patches are (re)loaded at title boot, so
// changes apply on the next launch. Runs on the UI thread.
void ShowPatchDialog(wxWindow* parent, const std::filesystem::path& patches_dir,
                     const std::string& title_id,
                     const std::string& title_name);

}  // namespace wx_ui
}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_WX_PATCH_DIALOG_H_
