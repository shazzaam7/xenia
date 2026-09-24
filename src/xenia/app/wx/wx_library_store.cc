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

}  // namespace

bool LoadLibrary(const std::filesystem::path& storage_root,
                 std::vector<GameEntry>& entries_out) {
  entries_out.clear();
  std::ifstream file(LibraryFile(storage_root));
  if (!file.is_open()) {
    return true;  // No library yet; not an error.
  }
  toml::parse_result parsed;
  try {
    parsed = toml::parse(file);
  } catch (toml::parse_error& e) {
    XELOGE("Cannot parse file: library.toml. Error: {}", e.what());
    return false;
  }
  if (!parsed.is_table()) {
    return true;
  }
  for (const auto& [title_id, node] : *parsed.as_table()) {
    if (!node.is_table()) {
      continue;
    }
    const toml::table* t = node.as_table();
    GameEntry entry;
    entry.title_id = std::string(title_id.str());
    if (auto v = t->get_as<std::string>("name")) {
      entry.name = v->get();
    }
    if (auto arr = t->get_as<toml::array>("media_ids")) {
      for (const auto& m : *arr) {
        if (m.is_string()) {
          entry.media_ids.push_back(std::string(m.as_string()->get()));
        }
      }
    }
    if (auto v = t->get_as<int64_t>("last_play")) {
      entry.last_play = std::time_t(v->get());
    }
    if (auto v = t->get_as<int64_t>("last_played_disc")) {
      entry.last_played_disc = int(v->get());
    }
    if (auto v = t->get_as<std::string>("compat")) {
      entry.compat = v->get();
    }
    if (auto v = t->get_as<std::string>("compat_url")) {
      entry.compat_url = v->get();
    }
    if (auto arr = t->get_as<toml::array>("discs")) {
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
        if (disc.label.empty()) {
          disc.label = "Disc " + std::to_string(disc.number);
        }
        entry.discs.push_back(std::move(disc));
      }
    }
    if (entry.title_id.empty() || entry.discs.empty()) {
      continue;
    }
    if (entry.last_played_disc < 1) {
      entry.last_played_disc = 1;
    }
    if (PruneEntry(entry)) {
      entries_out.push_back(std::move(entry));
    }
  }
  return true;
}

bool SaveLibrary(const std::filesystem::path& storage_root,
                 const std::vector<GameEntry>& entries) {
  auto table = toml::table();
  for (const auto& entry : entries) {
    if (entry.title_id.empty() || entry.discs.empty()) {
      continue;
    }
    auto t = toml::table();
    t.insert("name", entry.name);
    auto media = toml::array();
    for (const auto& m : entry.media_ids) {
      media.push_back(m);
    }
    t.insert("media_ids", std::move(media));
    t.insert("last_play", int64_t(entry.last_play));
    t.insert("last_played_disc", int64_t(entry.last_played_disc));
    if (!entry.compat.empty()) {
      t.insert("compat", entry.compat);
    }
    if (!entry.compat_url.empty()) {
      t.insert("compat_url", entry.compat_url);
    }
    auto discs = toml::array();
    for (const auto& disc : entry.discs) {
      auto d = toml::table();
      d.insert("number", int64_t(disc.number));
      d.insert("label", disc.label);
      d.insert("path", xe::path_to_utf8(disc.path));
      discs.push_back(std::move(d));
    }
    t.insert("discs", std::move(discs));
    table.insert(entry.title_id, std::move(t));
  }
  std::ofstream file(LibraryFile(storage_root), std::ofstream::trunc);
  if (!file.is_open()) {
    XELOGE("Cannot write file: library.toml.");
    return false;
  }
  file << table;
  return true;
}

void MergeScannedGame(std::vector<GameEntry>& entries,
                      const std::filesystem::path& disc_path,
                      const std::string& title_id, const std::string& media_id,
                      const std::string& name) {
  if (title_id.empty()) {
    return;
  }
  auto same_path = [&](const std::filesystem::path& a) {
    std::error_code ec = {};
    return std::filesystem::equivalent(a, disc_path, ec);
  };
  for (auto& entry : entries) {
    if (entry.title_id != title_id) {
      continue;
    }
    for (const auto& disc : entry.discs) {
      if (same_path(disc.path)) {
        return;  // Already listed.
      }
    }
    if (!media_id.empty() && media_id != "00000000" &&
        std::find(entry.media_ids.begin(), entry.media_ids.end(), media_id) ==
            entry.media_ids.end()) {
      entry.media_ids.push_back(media_id);
    }
    int number = int(entry.discs.size()) + 1;
    entry.discs.push_back(
        GameDisc{number, "Disc " + std::to_string(number), disc_path});
    return;
  }
  GameEntry entry;
  entry.title_id = title_id;
  entry.name = name;
  if (!media_id.empty()) {
    entry.media_ids.push_back(media_id);
  }
  entry.discs.push_back(GameDisc{1, "Disc 1", disc_path});
  entries.push_back(std::move(entry));
}

}  // namespace wx_ui
}  // namespace app
}  // namespace xe
