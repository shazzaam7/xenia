/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_WX_GAME_SCAN_DIALOG_H_
#define XENIA_APP_WX_GAME_SCAN_DIALOG_H_

#include <filesystem>
#include <string>
#include <vector>

#include "xenia/app/wx/wx_compat_db.h"
#include "xenia/app/wx/wx_game_scan.h"

class wxWindow;

namespace xe {
namespace app {
namespace wx_ui {

class WxWindow;

// How the user wants to supply games/content: explicit files or a folder
// scan. Shared by the game picker, the content picker, and their entry
// points so the choice looks and behaves the same everywhere.
enum class AddSource {
  kCancel,
  kFiles,
  kFolder,
};

// Modal one-click choice between picking files and scanning a folder.
AddSource AskAddSource(wxWindow* parent, const std::string& title,
                       const std::string& prompt);
// Same, resolving the parent from the backend window.
AddSource AskAddSource(WxWindow* window, const std::string& title,
                       const std::string& prompt);

// One scannable disc: the discovered path plus its readable metadata.
struct ScannedGameItem {
  std::filesystem::path path;
  GameMeta meta;
};

// Scans a folder for games not already in the library under one cancellable
// progress dialog: discovery (indeterminate) then metadata reads
// (determinate). known_paths holds library disc paths; discovered files
// matching one (equivalent()) are skipped, as are unreadable entries
// (omitted silently, same logging as the importer).
std::vector<ScannedGameItem> CollectNewGamesFromFolder(
    wxWindow* parent, const std::filesystem::path& dir,
    const std::vector<std::filesystem::path>& known_paths);

// Same as above for an explicit file list (Add Game): no discovery phase,
// just membership filtering and metadata reads under one progress dialog.
std::vector<ScannedGameItem> CollectNewGamesFromFiles(
    wxWindow* parent, const std::vector<std::filesystem::path>& paths,
    const std::vector<std::filesystem::path>& known_paths);

// Modal multi-select over scanned games. Every row starts checked. compat
// supplies the rating shown beside each title (missing IDs show none).
// known_paths lets the in-dialog "Add More" action skip library discs
// without the caller. Returns the checked paths;
// empty when cancelled or nothing is checked.
std::vector<std::filesystem::path> ShowScanPickerDialog(
    wxWindow* parent, const std::vector<ScannedGameItem>& items,
    const CompatMap& compat,
    const std::vector<std::filesystem::path>& known_paths);

}  // namespace wx_ui
}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_WX_GAME_SCAN_DIALOG_H_
