/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/app/wx/wx_library_store.h"

#include <algorithm>
#include <cctype>
#include <fstream>

#include "third_party/tomlplusplus/toml.hpp"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"

namespace xe {
namespace app {
namespace wx_ui {

std::string GameEntry::LocationLabel() const {
  if (discs.empty()) {
    return "";
  }
  std::string label = xe::path_to_utf8(discs[0].path);
  if (discs.size() > 1) {
    label += " (+" + std::to_string(discs.size() - 1) + ")";
  }
  return label;
}

namespace {

// Uppercase hex folder names: the scan emits them, but hand-made folders
// may not be, and Linux filesystems are case-sensitive.
std::string NormalizeTitleId(std::string title_id) {
  for (char& c : title_id) {
    c = char(std::toupper(static_cast<unsigned char>(c)));
  }
  return title_id;
}

// '/' separators avoid TOML escaping and round-trip through std::filesystem
// on every platform.
std::string PathToToml(const std::filesystem::path& path) {
  std::string s = xe::path_to_utf8(path);
  std::replace(s.begin(), s.end(), '\\', '/');
  return s;
}

bool PruneEntry(GameEntry& entry) {
  std::error_code ec = {};
  std::vector<GameDisc> kept;
  for (auto& disc : entry.discs) {
    if (!disc.path.empty() && std::filesystem::exists(disc.path, ec)) {
      disc.number = int(kept.size()) + 1;
      kept.push_back(std::move(disc));
    }
  }
  entry.discs = std::move(kept);
  if (entry.last_played_disc > int(entry.discs.size())) {
    entry.last_played_disc = 1;
  }
  return !entry.discs.empty();
}

bool ReadEntry(const std::filesystem::path& info_path,
               const std::string& title_id, GameEntry& entry_out) {
  toml::table parsed;
  try {
    parsed = toml::parse_file(xe::path_to_utf8(info_path));
  } catch (const toml::parse_error& e) {
    XELOGE("Library: cannot parse {}: {}", xe::path_to_utf8(info_path),
           e.what());
    return false;
  }
  GameEntry entry;
  entry.title_id = title_id;
  if (auto v = parsed.get_as<std::string>("name")) {
    entry.name = v->get();
  }
  if (auto c = parsed.get_as<toml::table>("compat")) {
    if (auto v = c->get_as<std::string>("state")) {
      entry.compat.state = v->get();
    }
    if (auto v = c->get_as<std::string>("url")) {
      entry.compat.url = v->get();
    }
  }
  if (auto v = parsed.get_as<int64_t>("last_play")) {
    entry.last_play = std::time_t(v->get());
  }
  if (auto v = parsed.get_as<int64_t>("last_played_disc")) {
    entry.last_played_disc = int(v->get());
  }
  if (auto arr = parsed.get_as<toml::array>("discs")) {
    for (const auto& d : *arr) {
      if (!d.is_table()) {
        continue;
      }
      const toml::table* dt = d.as_table();
      GameDisc disc;
      if (auto v = dt->get_as<int64_t>("number")) {
        disc.number = int(v->get());
      } else {
        disc.number = int(entry.discs.size()) + 1;
      }
      if (auto v = dt->get_as<std::string>("label")) {
        disc.label = v->get();
      }
      if (auto v = dt->get_as<std::string>("path")) {
        disc.path = xe::to_path(v->get());
      }
      if (auto v = dt->get_as<std::string>("media_id")) {
        disc.media_id = v->get();
      }
      if (auto v = dt->get_as<std::string>("version")) {
        disc.version = v->get();
      }
      if (disc.label.empty()) {
        disc.label = "Disc " + std::to_string(disc.number);
      }
      if (disc.media_id.empty()) {
        disc.media_id = "00000000";
      }
      entry.discs.push_back(std::move(disc));
    }
  }
  if (entry.discs.empty()) {
    return false;
  }
  if (entry.last_played_disc < 1) {
    entry.last_played_disc = 1;
  }
  entry_out = std::move(entry);
  return true;
}

}  // namespace

bool LoadLibrary(const std::filesystem::path& storage_root,
                 std::vector<GameEntry>& entries_out) {
  entries_out.clear();
  std::error_code ec = {};
  const auto root = LibraryRoot(storage_root);
  if (!std::filesystem::exists(root, ec)) {
    return true;  // No library yet; not an error.
  }
  std::vector<std::string> title_ids;
  for (auto it = std::filesystem::directory_iterator(root, ec);
       it != std::filesystem::directory_iterator(); ++it) {
    if (ec) {
      break;
    }
    std::error_code ec2 = {};
    if (!it->is_directory(ec2)) {
      continue;
    }
    title_ids.push_back(xe::path_to_utf8(it->path().filename()));
  }
  std::sort(title_ids.begin(), title_ids.end());
  for (const auto& title_id : title_ids) {
    GameEntry entry;
    if (!ReadEntry(InfoPath(storage_root, title_id), NormalizeTitleId(title_id),
                   entry)) {
      continue;
    }
    if (PruneEntry(entry)) {
      entries_out.push_back(std::move(entry));
    } else {
      // All discs vanished: drop the title folder (metadata and artwork).
      std::error_code ec3 = {};
      std::filesystem::remove_all(TitleDir(storage_root, title_id), ec3);
    }
  }
  return true;
}

bool WriteEntry(const std::filesystem::path& storage_root,
                const GameEntry& entry) {
  if (entry.title_id.empty() || entry.discs.empty()) {
    return false;
  }
  const auto dir = TitleDir(storage_root, entry.title_id);
  std::error_code ec = {};
  std::filesystem::create_directories(dir, ec);
  if (ec) {
    XELOGE("Library: cannot create {}: {}", xe::path_to_utf8(dir),
           ec.message());
    return false;
  }
  auto t = toml::table();
  t.insert("name", entry.name);
  if (!entry.compat.state.empty() || !entry.compat.url.empty()) {
    auto c = toml::table();
    c.insert("state", entry.compat.state);
    c.insert("url", entry.compat.url);
    c.is_inline(true);
    t.insert("compat", std::move(c));
  }
  t.insert("last_play", int64_t(entry.last_play));
  t.insert("last_played_disc", int64_t(entry.last_played_disc));
  auto discs = toml::array();
  for (const auto& disc : entry.discs) {
    auto d = toml::table();
    d.insert("number", int64_t(disc.number));
    d.insert("label", disc.label);
    d.insert("path", PathToToml(disc.path));
    d.insert("media_id", disc.media_id);
    d.insert("version", disc.version);
    d.is_inline(true);
    discs.push_back(std::move(d));
  }
  t.insert("discs", std::move(discs));
  const auto final_path = dir / "info.toml";
  const auto tmp_path = dir / "info.toml.tmp";
  {
    std::ofstream file(tmp_path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
      XELOGE("Library: cannot write {}.", xe::path_to_utf8(tmp_path));
      return false;
    }
    file << t;
    if (!file) {
      XELOGE("Library: write failed for {}.", xe::path_to_utf8(tmp_path));
      return false;
    }
  }
  std::filesystem::rename(tmp_path, final_path, ec);
  if (ec) {
    XELOGE("Library: cannot replace {}: {}", xe::path_to_utf8(final_path),
           ec.message());
    std::filesystem::remove(tmp_path, ec);
    return false;
  }
  return true;
}

bool RemoveTitle(const std::filesystem::path& storage_root,
                 const std::string& title_id) {
  if (title_id.empty()) {
    return false;
  }
  std::error_code ec = {};
  std::filesystem::remove_all(TitleDir(storage_root, title_id), ec);
  if (ec) {
    XELOGE("Library: cannot remove {}: {}", title_id, ec.message());
    return false;
  }
  return true;
}

const GameEntry* FindEntry(const std::vector<GameEntry>& entries,
                           const std::string& title_id) {
  const std::string want = NormalizeTitleId(title_id);
  for (const auto& entry : entries) {
    if (NormalizeTitleId(entry.title_id) == want) {
      return &entry;
    }
  }
  return nullptr;
}

GameEntry* FindEntry(std::vector<GameEntry>& entries,
                     const std::string& title_id) {
  return const_cast<GameEntry*>(
      FindEntry(const_cast<const std::vector<GameEntry>&>(entries), title_id));
}

bool MergeScannedGame(std::vector<GameEntry>& entries,
                      const std::filesystem::path& disc_path,
                      const std::string& title_id, const std::string& media_id,
                      const std::string& version, const std::string& name) {
  const std::string id = NormalizeTitleId(title_id);
  if (id.empty() || disc_path.empty()) {
    return false;
  }
  auto same_path = [&](const std::filesystem::path& a) {
    std::error_code ec = {};
    return std::filesystem::equivalent(a, disc_path, ec);
  };
  for (auto& entry : entries) {
    if (NormalizeTitleId(entry.title_id) != id) {
      continue;
    }
    for (auto& disc : entry.discs) {
      if (same_path(disc.path)) {
        // Already listed: refresh per-disc metadata from the rescan.
        bool changed = false;
        if (!media_id.empty() && disc.media_id != media_id) {
          disc.media_id = media_id;
          changed = true;
        }
        if (disc.version != version) {
          disc.version = version;
          changed = true;
        }
        return changed;
      }
    }
    int number = int(entry.discs.size()) + 1;
    GameDisc disc{number, "Disc " + std::to_string(number), disc_path,
                  media_id.empty() ? "00000000" : media_id, version};
    entry.discs.push_back(std::move(disc));
    if (entry.name.empty() && !name.empty()) {
      entry.name = name;
    }
    return true;
  }
  GameEntry entry;
  entry.title_id = id;
  entry.name = name;
  entry.discs.push_back(GameDisc{1, "Disc 1", disc_path,
                                 media_id.empty() ? "00000000" : media_id,
                                 version});
  entries.push_back(std::move(entry));
  return true;
}

}  // namespace wx_ui
}  // namespace app
}  // namespace xe
