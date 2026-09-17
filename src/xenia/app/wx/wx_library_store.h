/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_WX_LIBRARY_STORE_H_
#define XENIA_APP_WX_LIBRARY_STORE_H_

#include <filesystem>
#include <vector>

#include "xenia/app/wx/wx_game_model.h"

namespace xe {
namespace app {
namespace wx_ui {

// library.toml lives next to recent.toml in the storage root. Artwork is
// derived by convention and never stored here.
inline std::filesystem::path LibraryFile(
    const std::filesystem::path& storage_root) {
  return storage_root / "library.toml";
}
inline std::filesystem::path ArtworkDir(
    const std::filesystem::path& storage_root, const std::string& title_id) {
  return storage_root / "artwork" / title_id;
}
inline std::filesystem::path ArtIconPath(
    const std::filesystem::path& storage_root, const std::string& title_id) {
  return ArtworkDir(storage_root, title_id) / "icon.png";
}
inline std::filesystem::path ArtIconAltPath(
    const std::filesystem::path& storage_root, const std::string& title_id) {
  return ArtworkDir(storage_root, title_id) / "icon-alt.jpg";
}
inline std::filesystem::path ArtBackgroundPath(
    const std::filesystem::path& storage_root, const std::string& title_id) {
  return ArtworkDir(storage_root, title_id) / "background.jpg";
}

// Prunes discs whose files vanished and drops empty entries.
bool LoadLibrary(const std::filesystem::path& storage_root,
                 std::vector<GameEntry>& entries_out);
bool SaveLibrary(const std::filesystem::path& storage_root,
                 const std::vector<GameEntry>& entries);

// Merges one scanned disc into the library: appends to the existing title ID
// row (multi-disc) or creates a new row. Dedups by path.
void MergeScannedGame(std::vector<GameEntry>& entries,
                      const std::filesystem::path& disc_path,
                      const std::string& title_id, const std::string& media_id,
                      const std::string& name);

}  // namespace wx_ui
}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_WX_LIBRARY_STORE_H_
