/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_WX_GAME_ART_H_
#define XENIA_APP_WX_GAME_ART_H_

#include <filesystem>
#include <string>
#include <vector>

#include "xenia/app/wx/wx_game_scan.h"

namespace xe {
namespace app {
namespace wx_ui {

// Writes artwork/<TITLEID>/icon.png (128px PNG from embedded bytes) and
// background.jpg (raw nxebg.jpg from the embedded nxeart package, when the
// title ships one). Never touches the network. Returns true when icon.png
// exists afterwards; a missing icon is not an error (the view shows a
// placeholder).
bool EnsureArtwork(const std::filesystem::path& storage_root,
                   const std::filesystem::path& disc_path, GameFileType type,
                   const std::vector<uint8_t>& icon_bytes,
                   const std::string& title_id);

}  // namespace wx_ui
}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_WX_GAME_ART_H_
