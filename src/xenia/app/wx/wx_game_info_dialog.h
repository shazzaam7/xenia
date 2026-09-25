/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_WX_GAME_INFO_DIALOG_H_
#define XENIA_APP_WX_GAME_INFO_DIALOG_H_

#include <filesystem>

#include "xenia/app/wx/wx_game_model.h"

class wxWindow;

namespace xe {
namespace app {
namespace wx_ui {

// Modal editor for one library entry: display name, disc labels/paths/order,
// and artwork. Title ID, per-disc version/media IDs, and compatibility data
// are display-only (owned by the scan and the compatibility database).
//
// Edits *entry in place and returns true when the caller should persist and
// refresh (Save, or artwork changed even on Cancel). Returns false when
// nothing changed.
bool ShowGameInfoDialog(wxWindow* parent,
                        const std::filesystem::path& storage_root,
                        GameEntry* entry);

}  // namespace wx_ui
}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_WX_GAME_INFO_DIALOG_H_
