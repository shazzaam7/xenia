/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/app/wx/wx_compat_db.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
#include <thread>

#include <wx/bitmap.h>
#include <wx/colour.h>
#include <wx/image.h>
#include <wx/string.h>

#ifdef XENIA_HAS_CURL
#include <curl/curl.h>
#endif

#include "rapidjson/document.h"

#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"

namespace xe {
namespace app {
namespace wx_ui {
namespace {

// Download source for the compatibility data.
constexpr std::string_view kCompatDataUrl =
    "https://github.com/xenia-canary/game-compatibility/releases/download/"
    "game-compatibility/compatibility_data.json";

std::string ToUpperHex(std::string text) {
  // ASCII-only: title IDs are hex; std::toupper is locale-mapped.
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
    return c >= 'a' && c <= 'z' ? char(c - ('a' - 'A')) : char(c);
  });
  return text;
}

}  // namespace

std::string CompatRatingId(CompatRating rating) {
  switch (rating) {
    case CompatRating::kPlayable:
      return "Playable";
    case CompatRating::kGameplay:
      return "Gameplay";
    case CompatRating::kLoads:
      return "Loads";
    case CompatRating::kUnplayable:
      return "Unplayable";
    case CompatRating::kUnknown:
    default:
      return "Unknown";
  }
}

CompatRating CompatRatingFromId(std::string_view name) {
  if (name == "Playable") {
    return CompatRating::kPlayable;
  }
  if (name == "Gameplay") {
    return CompatRating::kGameplay;
  }
  if (name == "Loads") {
    return CompatRating::kLoads;
  }
  if (name == "Unplayable") {
    return CompatRating::kUnplayable;
  }
  return CompatRating::kUnknown;
}

bool ParseCompatJson(const std::string& json, CompatMap* out) {
  if (!out) {
    return false;
  }
  rapidjson::Document doc;
  doc.Parse(json.data(), json.size());
  if (doc.HasParseError() || !doc.IsArray()) {
    return false;
  }
  CompatMap map;
  for (const auto& entry : doc.GetArray()) {
    if (!entry.IsObject()) {
      continue;
    }
    const auto id_it = entry.FindMember("id");
    const auto state_it = entry.FindMember("state");
    if (id_it == entry.MemberEnd() || state_it == entry.MemberEnd()) {
      continue;
    }
    if (!id_it->value.IsString() || !state_it->value.IsString()) {
      continue;
    }
    std::string id = ToUpperHex(id_it->value.GetString());
    if (id.size() != 8 || !std::ranges::all_of(id, [](unsigned char c) {
          return std::isxdigit(c) != 0;
        })) {
      continue;
    }
    CompatInfo info;
    info.rating = CompatRatingFromId(std::string_view(
        state_it->value.GetString(), state_it->value.GetStringLength()));
    const auto url_it = entry.FindMember("url");
    if (url_it != entry.MemberEnd() && url_it->value.IsString()) {
      info.url.assign(url_it->value.GetString(),
                      url_it->value.GetStringLength());
    }
    // Most-optimistic state wins when an ID appears more than once.
    auto& slot = map[id];
    if (static_cast<uint8_t>(info.rating) > static_cast<uint8_t>(slot.rating)) {
      slot.rating = info.rating;
    }
    if (slot.url.empty()) {
      slot.url = std::move(info.url);
    }
  }
  *out = std::move(map);
  return true;
}

bool LoadCompatFile(const std::filesystem::path& path, CompatMap* out) {
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) {
    return false;
  }
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    return false;
  }
  std::ostringstream contents;
  contents << input.rdbuf();
  if (input.bad()) {
    return false;
  }
  CompatMap map;
  if (!ParseCompatJson(contents.str(), &map)) {
    XELOGE("CompatDb: failed to parse {}", xe::path_to_utf8(path));
    return false;
  }
  *out = std::move(map);
  return true;
}

const CompatInfo* FindCompat(const CompatMap& map,
                             const std::string& title_id) {
  if (title_id.size() != 8) {
    return nullptr;
  }
  const auto it = map.find(ToUpperHex(title_id));
  return it != map.end() ? &it->second : nullptr;
}

std::string CompatIssueSearchUrl(const std::string& title_id) {
  return "https://github.com/xenia-canary/game-compatibility/issues"
         "?q=is%3Aissue+is%3Aopen+" +
         ToUpperHex(title_id);
}

wxColour CompatColor(CompatRating rating) {
  switch (rating) {
    case CompatRating::kPlayable:
      return wxColour(0x22, 0x8B, 0x22);  // ForestGreen
    case CompatRating::kGameplay:
      return wxColour(0xAD, 0xFF, 0x2F);  // GreenYellow
    case CompatRating::kLoads:
      return wxColour(0xFF, 0xFF, 0x00);  // Yellow
    case CompatRating::kUnplayable:
      return wxColour(0xFF, 0x00, 0x00);  // Red
    case CompatRating::kUnknown:
    default:
      return wxColour(0xA9, 0xA9, 0xA9);  // DarkGray
  }
}

wxString CompatName(CompatRating rating) {
  switch (rating) {
    case CompatRating::kPlayable:
      return "Playable";
    case CompatRating::kGameplay:
      return "Gameplay";
    case CompatRating::kLoads:
      return "Loads";
    case CompatRating::kUnplayable:
      return "Unplayable";
    case CompatRating::kUnknown:
    default:
      return "Unknown";
  }
}

wxBitmap MakeCompatBall(CompatRating rating, int size_px) {
  const float radius = size_px * 0.42f;
  // Black outline ring so the badge reads on any artwork.
  const float border = (std::max)(1.0f, size_px * 0.09f);
  const float inner = radius - border;
  const wxColour color = CompatColor(rating);
  wxImage image(size_px, size_px);
  image.SetAlpha();
  unsigned char* rgb = image.GetData();
  unsigned char* alpha = image.GetAlpha();
  std::memset(alpha, 0, static_cast<size_t>(size_px) * size_px);
  const float cx = size_px * 0.5f;
  const float cy = size_px * 0.5f;
  for (int y = 0; y < size_px; ++y) {
    for (int x = 0; x < size_px; ++x) {
      const float dx = x + 0.5f - cx;
      const float dy = y + 0.5f - cy;
      const float d = std::sqrt(dx * dx + dy * dy);
      const float a_outer = std::clamp(radius - d + 0.5f, 0.0f, 1.0f);
      if (a_outer <= 0.0f) {
        continue;
      }
      const size_t pi = (static_cast<size_t>(y) * size_px + x) * 3;
      if (d <= inner) {
        const float a =
            (std::min)(a_outer, std::clamp(inner - d + 0.5f, 0.0f, 1.0f));
        rgb[pi + 0] = color.Red();
        rgb[pi + 1] = color.Green();
        rgb[pi + 2] = color.Blue();
        alpha[y * size_px + x] = static_cast<unsigned char>(a * 255.0f + 0.5f);
      } else {
        rgb[pi + 0] = 0;
        rgb[pi + 1] = 0;
        rgb[pi + 2] = 0;
        alpha[y * size_px + x] =
            static_cast<unsigned char>(a_outer * 255.0f + 0.5f);
      }
    }
  }
  return wxBitmap(image);
}

std::filesystem::path CompatCachePath(
    const std::filesystem::path& storage_root) {
  return storage_root / "compatibility_data.json";
}

bool CompatCacheFresh(const std::filesystem::path& storage_root) {
  const auto cache = CompatCachePath(storage_root);
  std::error_code ec;
  const auto mtime = std::filesystem::last_write_time(cache, ec);
  if (ec) {
    return false;
  }
  return (std::chrono::file_clock::now() - mtime) <
         std::chrono::hours(kCompatCacheMaxAgeHours);
}

namespace {

#ifdef XENIA_HAS_CURL
size_t CurlWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* body = static_cast<std::string*>(userdata);
  const size_t bytes = size * nmemb;
  body->append(ptr, bytes);
  return bytes;
}

bool DownloadCompatData(std::string* out) {
  const std::string url(kCompatDataUrl);
  return HttpGet(url, out);
}
#endif  // XENIA_HAS_CURL

bool WriteCacheAtomically(const std::filesystem::path& cache,
                          const std::string& body) {
  std::error_code ec;
  std::filesystem::create_directories(cache.parent_path(), ec);
  const auto tmp = std::filesystem::path(cache.string() + ".tmp");
  {
    std::ofstream output(tmp, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
      return false;
    }
    output.write(body.data(), static_cast<std::streamsize>(body.size()));
    output.close();
    if (output.fail()) {
      std::filesystem::remove(tmp, ec);
      return false;
    }
  }
  std::filesystem::rename(tmp, cache, ec);
  return !ec;
}

}  // namespace

bool HttpGet(const std::string& url, std::string* body) {
#ifdef XENIA_HAS_CURL
  if (!body) {
    return false;
  }
  // One-time process init (call_once: curl_global_init is not thread-safe).
  static std::once_flag init_once;
  std::call_once(init_once, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });
  // One easy handle per thread, reused across calls: DNS and TLS/TCP
  // connections stay warm instead of re-handshaking every file.
  struct ThreadHandle {
    CURL* handle = nullptr;
    ~ThreadHandle() {
      if (handle) {
        curl_easy_cleanup(handle);
      }
    }
  };
  thread_local ThreadHandle thread_handle;
  if (!thread_handle.handle) {
    thread_handle.handle = curl_easy_init();
    if (!thread_handle.handle) {
      return false;
    }
  }
  CURL* curl = thread_handle.handle;
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, body);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "xenia-canary/1.0");
  const CURLcode rc = curl_easy_perform(curl);
  long http_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  return rc == CURLE_OK && http_code == 200 && !body->empty();
#else
  (void)url;
  (void)body;
  return false;
#endif  // XENIA_HAS_CURL
}

void FetchCompatDataAsync(std::filesystem::path storage_root, bool force,
                          CompatFetchCallback done) {
  std::thread([storage_root = std::move(storage_root), force,
               done = std::move(done)]() mutable {
    CompatMap map;
    const auto cache = CompatCachePath(storage_root);
    if (!force && CompatCacheFresh(storage_root) &&
        LoadCompatFile(cache, &map)) {
      done(true, std::move(map));
      return;
    }
#ifdef XENIA_HAS_CURL
    std::string body;
    if (DownloadCompatData(&body) && ParseCompatJson(body, &map)) {
      // A cache write failure must not fail the data itself.
      if (!WriteCacheAtomically(cache, body)) {
        XELOGW(
            "CompatDb: downloaded data is live but the cache write "
            "failed: {}",
            xe::path_to_utf8(cache));
      }
      done(true, std::move(map));
      return;
    }
    XELOGE("CompatDb: download failed, falling back to on-disk cache");
#else
    XELOGW(
        "CompatDb: no download support in this build, using on-disk "
        "cache");
#endif  // XENIA_HAS_CURL
    done(LoadCompatFile(cache, &map), std::move(map));
  }).detach();
}

}  // namespace wx_ui
}  // namespace app
}  // namespace xe
