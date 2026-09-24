/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CONFIG_H_
#define XENIA_CONFIG_H_

#include <filesystem>
#include <map>
#include <set>
#include <string>
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wabsolute-value"
#endif
#include "third_party/tomlplusplus/toml.hpp"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

toml::parse_result ParseFile(const std::filesystem::path& filename);

namespace config {
void SetupConfig(const std::filesystem::path& config_folder);
// Absolute path of the per-title override file -
// <storage_root>/config/<TITLEID>.config.toml - which is also the file
// LoadGameConfig reads. Empty before SetupConfig.
std::filesystem::path GameConfigPath(const std::string& title_id);
// Applies the per-title override file over the global config.
void LoadGameConfig(const std::string_view title_id);
// Drops every per-game override, so the global config values apply again.
void ClearGameConfig();
// Re-reads the global config file from disk, so external edits (or a
// spawned child inheriting stale cvars) pick up current values. No-op when
// no config file is set. Does not touch per-game overrides.
void ReloadConfig();
// Removes the named settings (cvar names) from an existing per-title override
// file, leaving every other line - the header, unrelated overrides and their
// comments - untouched. Used when a stored override no longer fits the type its
// variable has now, so it can never be read back; leaving it in place would
// keep the title pinned to a value nothing applies. A missing or unreadable
// file is left alone.
void PruneGameConfigFile(const std::filesystem::path& file_path,
                         const std::set<std::string>& keys);
void SaveConfig();
// Writes the sparse per-title override file from the cvars' per-game layers
// only, deleting it instead when no overrides are left (which means "use the
// global config"). `title_name` fills the header comment; `reasons` maps a
// cvar name to the comment appended to its line and may be null. Returns false
// only when an existing file could not be written.
bool SaveGameConfigSparse(const std::string& title_id,
                          const std::string& title_name,
                          const std::map<std::string, std::string>* reasons);
}  // namespace config

#endif  // XENIA_CONFIG_H_
