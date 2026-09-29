/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_WX_CONTENT_PICK_DIALOG_H_
#define XENIA_APP_WX_CONTENT_PICK_DIALOG_H_

#include <filesystem>
#include <vector>

#include "xenia/emulator.h"

namespace xe {
namespace app {
namespace wx_ui {

class WxWindow;

// Header-processes picked paths with a cancellable progress dialog (UI
// thread; each package is just a header read). Invalid packages are omitted
// silently. Appends to out, so it serves initial picks and in-dialog
// additions alike.
void PrepareContentEntries(WxWindow* window, Emulator* emulator,
                           const std::vector<std::filesystem::path>& paths,
                           std::vector<Emulator::ContentInstallEntry>& out);

// Recursive discovery of content packages: regular files with CON/LIVE/PIRS
// magic. Validation happens later in ProcessContentPackageHeader.
std::vector<std::filesystem::path> DiscoverContentPackages(
    const std::filesystem::path& dir);

enum class ContentPickAction {
  kCancel,
  kInstall,
  kExtract,
};

struct ContentPickResult {
  ContentPickAction action = ContentPickAction::kCancel;
  // Indices into entries for the checked rows.
  std::vector<size_t> checked;
  // Destination picked when the action is kExtract.
  std::filesystem::path extract_dir;
};

// Modal multi-select over prepared entries (invalid ones never reach here).
// Every row starts checked. entries is appended in place by the in-dialog
// Scan Folder / Add Files actions, so existing rows and their check states
// survive additions.
ContentPickResult ShowContentPickDialog(
    WxWindow* window, Emulator* emulator,
    std::vector<Emulator::ContentInstallEntry>& entries);

}  // namespace wx_ui
}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_WX_CONTENT_PICK_DIALOG_H_
