/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_WX_GAME_SCAN_H_
#define XENIA_APP_WX_GAME_SCAN_H_

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace xe {
namespace app {
namespace wx_ui {

enum class GameFileType {
  kUnknown,
  kXex,   // Loose .xex file (XEX1/XEX2/XEX%).
  kStfs,  // CON/LIVE/PIRS container with an STFS volume.
  kSvod,  // CON/LIVE/PIRS container with an SVOD volume (GOD).
  kIso,   // ISO/XISO/GDFX disc image.
  kZar,   // ZAR disc archive.
};

// Metadata for one disc file. icon_bytes holds embedded thumbnail bytes
// (PNG/JPEG) when the container header carries one; empty otherwise.
struct GameMeta {
  bool ok = false;
  GameFileType type = GameFileType::kUnknown;
  // 8 uppercase hex chars. Empty when the file has no usable title ID.
  std::string title_id;
  std::string media_id = "00000000";
  std::string name;
  int disc_number = 1;
  int disc_count = 1;
  std::vector<uint8_t> icon_bytes;
};

// Magic-sniff only. Directories are probed for extracted titles (default.xex
// or a top-level CON-like file).
GameFileType IdentifyFile(const std::filesystem::path& path);

// Full metadata read, following default.xex/nxeart chains inside containers.
// Returns ok=false when the file is not a recognizable game disc.
bool ReadGameMeta(const std::filesystem::path& path, GameMeta& meta_out);

// Breadth-first discovery of game files under a folder, following the same
// rules as the Manager scanner: XEX files collapse to default.xex, an XEX hit
// stops recursion except into a "content" subfolder, STFS files are filtered
// to game content types.
std::vector<std::filesystem::path> DiscoverGameFiles(
    const std::filesystem::path& directory);

// Discovery of installed titles under the content tree:
// <content_root>/0000000000000000/<TITLEID>/000D0000 is a container directory
// holding installed package files (returned as-is for ReadGameMeta/launch)
// and/or extracted package directories (contributing a launchable XEX in
// file form, like DiscoverGameFiles). Anything else is ignored; validation
// happens later in ReadGameMeta.
std::vector<std::filesystem::path> DiscoverInstalledGames(
    const std::filesystem::path& content_root);

// Reads a named file out of a container without mounting anything in the live
// VFS (standalone device instances). Name match is case-insensitive.
bool ReadContainerFile(const std::filesystem::path& container_path,
                       GameFileType container_type, const std::string& name,
                       std::vector<uint8_t>& bytes_out);

// Lists root-level file names inside a container (for default.xex fallback).
std::vector<std::string> ListContainerFiles(
    const std::filesystem::path& container_path, GameFileType container_type);

// Decrypts/decompresses a loose XEX file into its in-memory PE image and
// extracts the embedded SPA (title database) resource bytes.
bool ExtractXexSpa(const std::vector<uint8_t>& xex_bytes,
                   const std::string& title_id, std::vector<uint8_t>& spa_out);

// Reads title ID / media ID / disc numbers from XEX optional headers only
// (headers are plaintext; no crypto needed).
bool ReadXexMeta(const uint8_t* data, size_t size, GameMeta& meta_out);

// Reads a named file out of an in-memory STFS package (e.g. nxeart bytes).
// Name match is case-insensitive.
bool ReadStfsMemoryFile(const uint8_t* stfs_bytes, size_t size,
                        const std::string& name,
                        std::vector<uint8_t>& bytes_out);

}  // namespace wx_ui
}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_WX_GAME_SCAN_H_
