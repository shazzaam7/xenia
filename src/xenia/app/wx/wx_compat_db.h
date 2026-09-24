/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_WX_COMPAT_DB_H_
#define XENIA_APP_WX_COMPAT_DB_H_

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>

class wxBitmap;
class wxColour;
class wxString;

namespace xe {
namespace app {
namespace wx_ui {

// Compatibility rating for a title. Declaration order encodes "better
// compat" — higher values sort above lower ones.
enum class CompatRating : uint8_t {
  kUnknown = 0,
  kUnplayable,
  kLoads,
  kGameplay,
  kPlayable,
};

struct CompatInfo {
  CompatRating rating = CompatRating::kUnknown;
  std::string url;
};

// Title ID (uppercased 8-hex) -> report. Absent IDs read as Unknown.
using CompatMap = std::map<std::string, CompatInfo>;

// Parses compatibility_data.json contents (a JSON array of
// {id, title, state, url}). Unknown state strings become Unknown. Returns
// false when the document is not a JSON array.
bool ParseCompatJson(const std::string& json, CompatMap* out);

// Loads and parses a cache/data file. Returns false when the file cannot be
// read or parsed; `out` is left untouched on failure.
bool LoadCompatFile(const std::filesystem::path& path, CompatMap* out);

// Full entry for a title ID, or nullptr when the database has no report.
// Useful when the URL matters, not just the rating.
const CompatInfo* FindCompat(const CompatMap& map, const std::string& title_id);

// GitHub issue-search URL for titles without a report, scoped to the
// compatibility tracker with an `is:issue is:open <TITLEID>` query.
std::string CompatIssueSearchUrl(const std::string& title_id);

// Badge color for a rating (Xenia Manager palette).
wxColour CompatColor(CompatRating rating);

// Short display name for a rating ("Playable", ..., "Unknown").
wxString CompatName(CompatRating rating);

// Canonical rating name for persistence (library.toml); "Unknown" for
// Unknown. Round-trips through CompatRatingFromId.
std::string CompatRatingId(CompatRating rating);
// Parses a rating name; anything unrecognized becomes Unknown.
CompatRating CompatRatingFromId(std::string_view name);

// Antialiased filled circle used as the compatibility badge.
wxBitmap MakeCompatBall(CompatRating rating, int size_px);

// Cache location: <storage_root>/compatibility_data.json.
std::filesystem::path CompatCachePath(
    const std::filesystem::path& storage_root);

// Hours after which the cache is considered stale.
constexpr int kCompatCacheMaxAgeHours = 24;

// True when a usable cache file exists and is younger than the max age.
bool CompatCacheFresh(const std::filesystem::path& storage_root);

// Called with the fetch result on the fetch worker thread (never the UI
// thread): the receiver must marshal to the UI thread itself and must
// outlive the call (the fetch holds no UI references).
using CompatFetchCallback = std::function<void(bool success, CompatMap map)>;

// Loads compatibility data on a detached worker thread, then invokes `done`
// on that same thread. With force=false and a fresh cache, no download is
// attempted. With force=true (or a missing/stale cache) the data file is
// downloaded (only when built with libcurl, see XENIA_HAS_CURL), parsed,
// and atomically replaces the cache. Any download/parse failure falls back
// to the on-disk cache; `success` is false only when no usable data exists.
void FetchCompatDataAsync(std::filesystem::path storage_root, bool force,
                          CompatFetchCallback done);

}  // namespace wx_ui
}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_WX_COMPAT_DB_H_
