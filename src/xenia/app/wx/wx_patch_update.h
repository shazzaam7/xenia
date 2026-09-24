/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_WX_PATCH_UPDATE_H_
#define XENIA_APP_WX_PATCH_UPDATE_H_

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

class wxWindow;

namespace xe {
namespace app {
namespace wx_ui {

// Progress shared between the update worker thread and the UI poll timer.
// Counters are plain (UI reads them with the mutex held); cancellation is
// atomic so the worker can check it without locking.
struct PatchUpdateProgress {
  std::mutex mutex;
  size_t total = 0;  // Files listed upstream (0 while listing).
  size_t done = 0;
  size_t updated = 0;    // Written with new content.
  size_t unchanged = 0;  // Byte-identical, left alone.
  size_t failed = 0;
  size_t preserved = 0;  // Local is_enabled states carried into new files.
  std::string current;   // File being processed.
  // Fatal error (e.g. the file list could not be fetched). Set together
  // with finished; per-file failures only bump `failed`.
  std::string error;
  std::atomic<bool> cancelled{false};
  bool finished = false;
};

// Lists every *.patch.toml upstream, downloads each, and merges it over the
// local copy on a detached worker thread, updating `progress` as it goes.
// Local is_enabled states are carried over by patch name (positional fallback
// for unnamed entries); byte-identical files are skipped; each overwritten
// file keeps a .bak next to it. New content applies on the next title launch,
// like the patch editor. Without download support the worker finishes
// immediately with `error` set.
void UpdateGamePatchesAsync(const std::filesystem::path& patches_dir,
                            std::shared_ptr<PatchUpdateProgress> progress);

// Modal progress dialog (UI thread) for an update run. Returns after the
// worker finishes or the user cancels, then reports a summary.
void ShowPatchUpdateDialog(wxWindow* parent,
                           const std::filesystem::path& patches_dir);

}  // namespace wx_ui
}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_WX_PATCH_UPDATE_H_
