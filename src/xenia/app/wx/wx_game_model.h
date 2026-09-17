/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_WX_GAME_MODEL_H_
#define XENIA_APP_WX_GAME_MODEL_H_

#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

namespace xe {
namespace app {
namespace wx_ui {

// One disc belonging to a title. Disc 1 is discs[0]; numbering is 1-based.
struct GameDisc {
  int number = 1;
  std::string label;
  std::filesystem::path path;
};

// One library row: a single title ID with one or more disc paths.
// Artwork is derived by convention (artwork/<TITLEID>/icon.png,
// artwork/<TITLEID>/background.jpg) and intentionally not stored.
struct GameEntry {
  // 8 uppercase hex chars, e.g. "415407D2". Empty means unknown/invalid.
  std::string title_id;
  std::string name;
  // Media IDs, disc 1 first. Individual discs of a title may differ.
  std::vector<std::string> media_ids;
  std::vector<GameDisc> discs;
  std::time_t last_play = 0;
  int last_played_disc = 1;

  bool IsMultiDisc() const { return discs.size() > 1; }
  size_t DiscCount() const { return discs.empty() ? 0 : discs.size(); }

  const std::filesystem::path* DiscPath(int disc_number) const {
    for (const auto& disc : discs) {
      if (disc.number == disc_number) {
        return &disc.path;
      }
    }
    return discs.empty() ? nullptr : &discs[0].path;
  }

  std::string MediaIdLabel() const {
    return media_ids.empty() ? "00000000" : media_ids[0];
  }

  // "D:/Games/a.iso (+2)" for multi-disc; plain path otherwise.
  std::string LocationLabel() const;
};

}  // namespace wx_ui
}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_WX_GAME_MODEL_H_
