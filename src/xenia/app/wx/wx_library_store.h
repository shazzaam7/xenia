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
#include <string>
#include <vector>

#include "xenia/app/wx/wx_game_model.h"

namespace xe {
namespace app {
namespace wx_ui {

// One folder per title under the storage root: library/<TITLEID>/info.toml
// plus library/<TITLEID>/artwork/. Each title persists on its own, so a
// failed write can only affect that title. Artwork is derived by convention
// and never stored in info.toml.
inline std::filesystem::path LibraryRoot(
    const std::filesystem::path& storage_root) {
  return storage_root / "library";
}
inline std::filesystem::path TitleDir(const std::filesystem::path& storage_root,
                                      const std::string& title_id) {
  return LibraryRoot(storage_root) / title_id;
}
inline std::filesystem::path InfoPath(const std::filesystem::path& storage_root,
                                      const std::string& title_id) {
  return TitleDir(storage_root, title_id) / "info.toml";
}
inline std::filesystem::path ArtworkDir(
    const std::filesystem::path& storage_root, const std::string& title_id) {
  return TitleDir(storage_root, title_id) / "artwork";
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

// Reads every title folder. Prunes discs whose files vanished and drops
// (and deletes) titles left with none. Missing library root is not an error.
bool LoadLibrary(const std::filesystem::path& storage_root,
                 std::vector<GameEntry>& entries_out);
// Atomically persists one title (tmp file + rename). Creates the title
// folder as needed. False when the title id is empty or the write failed.
bool WriteEntry(const std::filesystem::path& storage_root,
                const GameEntry& entry);
// Deletes a title folder (metadata and artwork). True when nothing of the
// title remains afterwards (including when it never existed).
bool RemoveTitle(const std::filesystem::path& storage_root,
                 const std::string& title_id);

// Finds a loaded entry by title ID (case-insensitive hex). Null when absent.
const GameEntry* FindEntry(const std::vector<GameEntry>& entries,
                           const std::string& title_id);
GameEntry* FindEntry(std::vector<GameEntry>& entries,
                     const std::string& title_id);

// Merges one scanned disc into the library: appends to the existing title ID
// row (multi-disc) or creates a new row. Dedups by path; refreshes the
// disc's media/version when a known path is re-scanned. Returns true when
// the in-memory library changed (the caller persists the touched entry).
bool MergeScannedGame(std::vector<GameEntry>& entries,
                      const std::filesystem::path& disc_path,
                      const std::string& title_id, const std::string& media_id,
                      const std::string& version, const std::string& name);

}  // namespace wx_ui
}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_WX_LIBRARY_STORE_H_
