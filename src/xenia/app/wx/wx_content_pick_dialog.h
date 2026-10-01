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

#include <atomic>
#include <filesystem>
#include <set>
#include <vector>

#include "xenia/emulator.h"

class wxWindow;

namespace xe {
namespace app {
namespace wx_ui {

class WxWindow;

// Header-processes picked paths with a cancellable progress dialog (UI
// thread; each package is just a header read). Invalid packages are omitted
// silently, as are types outside allowed_types when it is non-empty.
// Appends to out, so it serves initial picks and in-dialog additions alike.
void PrepareContentEntries(WxWindow* window, Emulator* emulator,
                           const std::vector<std::filesystem::path>& paths,
                           std::vector<Emulator::ContentInstallEntry>& out,
                           const std::set<XContentType>& allowed_types);

// Modal checklist of content types to scan a folder for. All types start
// checked. An empty prompt selects the default message. Returns false on
// Cancel; chosen holds the selected types.
// Same, resolving the parent from the backend window.
bool AskContentTypes(WxWindow* window, std::set<XContentType>& chosen,
                     const std::string& prompt = {});

// Full folder pipeline with progress throughout and no UI-thread freezes:
// worker discovery (pulsed), type checklist, header reads (determinate).
// Appends to out; out is untouched when cancelled at any stage.
void CollectFolderEntries(WxWindow* window, Emulator* emulator,
                          const std::filesystem::path& dir,
                          std::vector<Emulator::ContentInstallEntry>& out);

// Recursive discovery of content packages: regular files with CON/LIVE/PIRS
// magic. Validation happens later in ProcessContentPackageHeader. Checks
// cancel every entry when non-null.
std::vector<std::filesystem::path> DiscoverContentPackages(
    const std::filesystem::path& dir,
    const std::atomic<bool>* cancel = nullptr);

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
