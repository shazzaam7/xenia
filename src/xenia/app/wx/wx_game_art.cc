/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/app/wx/wx_game_art.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include <wx/image.h>
#include <wx/mstream.h>

#include "xenia/app/wx/wx_library_store.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"

namespace xe {
namespace app {
namespace wx_ui {

namespace {

bool IsImageBytes(const std::vector<uint8_t>& bytes) {
  if (bytes.size() < 4) {
    return false;
  }
  static const uint8_t kPng[] = {0x89, 0x50, 0x4E, 0x47};
  static const uint8_t kJpeg[] = {0xFF, 0xD8, 0xFF};
  return std::memcmp(bytes.data(), kPng, 4) == 0 ||
         std::memcmp(bytes.data(), kJpeg, 3) == 0;
}

bool DimsSane(const wxImage& image) {
  return image.IsOk() && image.GetWidth() > 0 && image.GetHeight() > 0 &&
         image.GetWidth() <= 2048 && image.GetHeight() <= 2048;
}

// Full decode check: magic bytes alone let carved garbage through (a chance
// FF D8 FF hit inside STFS metadata still "looks like" a JPEG).
bool Decodes(const std::vector<uint8_t>& bytes) {
  if (bytes.size() < 4) {
    return false;
  }
  wxMemoryInputStream stream(bytes.data(), bytes.size());
  wxImage image;
  return image.LoadFile(stream) && DimsSane(image);
}

bool FileDecodes(const std::filesystem::path& path) {
  wxImage image;
  return image.LoadFile(wxString::FromUTF8(xe::path_to_utf8(path))) &&
         DimsSane(image);
}

bool SaveIconPng(const std::vector<uint8_t>& bytes,
                 const std::filesystem::path& dest) {
  if (!IsImageBytes(bytes)) {
    return false;
  }
  wxMemoryInputStream stream(bytes.data(), bytes.size());
  wxImage image;
  if (!image.LoadFile(stream) || !DimsSane(image)) {
    return false;
  }
  // Native size: the library view scales for display itself.
  std::error_code ec = {};
  std::filesystem::create_directories(dest.parent_path(), ec);
  return image.SaveFile(wxString::FromUTF8(xe::path_to_utf8(dest)),
                        wxBITMAP_TYPE_PNG);
}

// nxebg.jpg lives inside an nxeart STFS package (or IS the file already).
bool ExtractNxebg(const std::vector<uint8_t>& nxeart,
                  std::vector<uint8_t>& bg_out) {
  bg_out.clear();
  if (IsImageBytes(nxeart)) {
    if (!Decodes(nxeart)) {
      return false;
    }
    bg_out = nxeart;
    return true;
  }
  if (!ReadStfsMemoryFile(nxeart.data(), nxeart.size(), "nxebg.jpg", bg_out)) {
    return false;
  }
  return Decodes(bg_out);
}

bool ExtractNxeSlot(const std::vector<uint8_t>& nxeart,
                    std::vector<uint8_t>& slot_out) {
  slot_out.clear();
  if (IsImageBytes(nxeart)) {
    return false;
  }
  if (!ReadStfsMemoryFile(nxeart.data(), nxeart.size(), "nxeslot.jpg",
                          slot_out)) {
    return false;
  }
  return Decodes(slot_out);
}

bool SaveBackground(const std::vector<uint8_t>& nxeart,
                    const std::filesystem::path& dest) {
  std::vector<uint8_t> bg;
  if (!ExtractNxebg(nxeart, bg)) {
    return false;
  }
  std::error_code ec = {};
  std::filesystem::create_directories(dest.parent_path(), ec);
  FILE* f = xe::filesystem::OpenFile(dest, "wb");
  if (!f) {
    return false;
  }
  size_t n = std::fwrite(bg.data(), 1, bg.size(), f);
  std::fclose(f);
  return n == bg.size();
}

bool DecodeDims(const std::vector<uint8_t>& bytes, int* w_out, int* h_out) {
  wxMemoryInputStream stream(bytes.data(), bytes.size());
  wxImage image;
  if (!image.LoadFile(stream) || !DimsSane(image)) {
    return false;
  }
  *w_out = image.GetWidth();
  *h_out = image.GetHeight();
  return true;
}

bool DecodeFileDims(const std::filesystem::path& path, int* w_out, int* h_out) {
  wxImage image;
  if (!image.LoadFile(wxString::FromUTF8(xe::path_to_utf8(path))) ||
      !DimsSane(image)) {
    return false;
  }
  *w_out = image.GetWidth();
  *h_out = image.GetHeight();
  return true;
}

// True when the cached icon.png should be rewritten from fresh bytes: an
// unreadable cache, or a resize left over from the forced-128 era.
bool IconNeedsRefresh(const std::filesystem::path& icon_path,
                      const std::vector<uint8_t>& fresh_bytes) {
  int fw = 0, fh = 0;
  if (!DecodeDims(fresh_bytes, &fw, &fh)) {
    return false;
  }
  int cw = 0, ch = 0;
  if (!DecodeFileDims(icon_path, &cw, &ch)) {
    return true;
  }
  return cw != fw || ch != fh;
}

bool SaveSlot(const std::vector<uint8_t>& nxeart,
              const std::filesystem::path& dest) {
  std::vector<uint8_t> slot;
  if (!ExtractNxeSlot(nxeart, slot)) {
    return false;
  }
  std::error_code ec = {};
  std::filesystem::create_directories(dest.parent_path(), ec);
  FILE* f = xe::filesystem::OpenFile(dest, "wb");
  if (!f) {
    return false;
  }
  size_t n = std::fwrite(slot.data(), 1, slot.size(), f);
  std::fclose(f);
  return n == slot.size();
}

}  // namespace

bool EnsureArtwork(const std::filesystem::path& storage_root,
                   const std::filesystem::path& disc_path, GameFileType type,
                   const std::vector<uint8_t>& icon_bytes,
                   const std::string& title_id) {
  if (title_id.empty()) {
    return false;
  }
  auto icon_path = ArtIconPath(storage_root, title_id);
  auto icon_alt_path = ArtIconAltPath(storage_root, title_id);
  auto bg_path = ArtBackgroundPath(storage_root, title_id);
  std::error_code ec = {};
  // Self-heal caches from older builds (forced-128 icons, carved garbage):
  // drop what no longer validates so the paths below regenerate it.
  if (!icon_bytes.empty() && std::filesystem::exists(icon_path, ec) &&
      IconNeedsRefresh(icon_path, icon_bytes)) {
    XELOGW("Library: refreshing stale icon for {}.", title_id);
    std::filesystem::remove(icon_path, ec);
  }
  if (std::filesystem::exists(bg_path, ec) && !FileDecodes(bg_path)) {
    XELOGW("Library: dropping unreadable background for {}.", title_id);
    std::filesystem::remove(bg_path, ec);
  }
  if (std::filesystem::exists(icon_alt_path, ec) &&
      !FileDecodes(icon_alt_path)) {
    std::filesystem::remove(icon_alt_path, ec);
  }
  bool have_icon = std::filesystem::exists(icon_path, ec);
  if (!have_icon && !icon_bytes.empty()) {
    have_icon = SaveIconPng(icon_bytes, icon_path);
    if (!have_icon) {
      XELOGW("Library: failed to decode embedded icon for {}.", title_id);
    }
  }
  // nxeart handling: both nxebg.jpg -> background.jpg and nxeslot.jpg ->
  // icon-alt.jpg. Found via container search or alongside extracted XEX.
  std::vector<uint8_t> nxeart;
  bool have_nxeart = false;
  if (type == GameFileType::kXex) {
    std::filesystem::path dir = std::filesystem::is_directory(disc_path, ec)
                                    ? disc_path
                                    : disc_path.parent_path();
    std::vector<uint8_t> raw;
    FILE* f = xe::filesystem::OpenFile(dir / "nxeart", "rb");
    if (f) {
      std::fseek(f, 0, SEEK_END);
      long size = std::ftell(f);
      std::fseek(f, 0, SEEK_SET);
      if (size > 0 && size < 64 * 1024 * 1024) {
        raw.resize(size_t(size));
        raw.resize(std::fread(raw.data(), 1, raw.size(), f));
      }
      std::fclose(f);
      if (!raw.empty()) {
        nxeart = std::move(raw);
        have_nxeart = true;
      }
    }
    if (!have_nxeart) {
      // Fallback: search recursively under dir for a file named nxeart
      // (handles unusual extracted layouts).
      std::error_code sec = {};
      for (auto it = std::filesystem::recursive_directory_iterator(dir, sec);
           it != std::filesystem::recursive_directory_iterator(); ++it) {
        if (sec) {
          break;
        }
        if (!it->is_regular_file(sec)) {
          continue;
        }
        std::string name = xe::path_to_utf8(it->path().filename());
        std::string lower = name;
        std::transform(
            lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
              // ASCII-only (see wx_game_scan.cc Lower).
              return c >= 'A' && c <= 'Z' ? char(c + ('a' - 'A')) : char(c);
            });
        if (lower == "nxeart") {
          FILE* f2 = xe::filesystem::OpenFile(it->path(), "rb");
          if (f2) {
            std::fseek(f2, 0, SEEK_END);
            long sz = std::ftell(f2);
            std::fseek(f2, 0, SEEK_SET);
            if (sz > 0 && sz < 64 * 1024 * 1024) {
              raw.resize(size_t(sz));
              raw.resize(std::fread(raw.data(), 1, raw.size(), f2));
              if (!raw.empty()) {
                nxeart = std::move(raw);
                have_nxeart = true;
              }
            }
            std::fclose(f2);
            if (have_nxeart) {
              break;
            }
          }
        }
      }
    }
  } else if (type == GameFileType::kStfs || type == GameFileType::kSvod ||
             type == GameFileType::kIso || type == GameFileType::kZar) {
    // Root only: a disc can carry other titles' nxeart deeper in its tree
    // (Content/, $SystemUpdate/), and that must not become this game's art.
    // Plenty of games ship no nxeart at all; that is not an error.
    for (const auto& entry_name : ListContainerFiles(disc_path, type)) {
      std::string lower = entry_name;
      std::transform(
          lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
            // ASCII-only (see wx_game_scan.cc Lower).
            return c >= 'A' && c <= 'Z' ? char(c + ('a' - 'A')) : char(c);
          });
      if (lower != "nxeart") {
        continue;
      }
      if (ReadContainerFile(disc_path, type, entry_name, nxeart) &&
          !nxeart.empty()) {
        have_nxeart = true;
      }
      break;
    }
  }
  if (have_nxeart && !nxeart.empty()) {
    if (!std::filesystem::exists(bg_path, ec)) {
      if (!SaveBackground(nxeart, bg_path)) {
        XELOGW("Library: no usable background in nxeart for {}.", title_id);
      }
    }
    if (!std::filesystem::exists(icon_alt_path, ec)) {
      if (!SaveSlot(nxeart, icon_alt_path)) {
        // Not all nxeart contain nxeslot; not an error.
      }
    }
    // If we still have no icon.png, use nxeslot as a fallback.
    if (!have_icon && !std::filesystem::exists(icon_path, ec)) {
      std::vector<uint8_t> slot;
      if (ExtractNxeSlot(nxeart, slot)) {
        have_icon = SaveIconPng(slot, icon_path);
      }
    }
  }
  return have_icon;
}

bool SaveIconFile(const std::vector<uint8_t>& bytes,
                  const std::filesystem::path& dest) {
  if (!Decodes(bytes)) {
    return false;
  }
  return SaveIconPng(bytes, dest);
}

bool SaveBackgroundFile(const std::vector<uint8_t>& bytes,
                        const std::filesystem::path& dest) {
  if (!Decodes(bytes)) {
    return false;
  }
  std::error_code ec = {};
  std::filesystem::create_directories(dest.parent_path(), ec);
  FILE* f = xe::filesystem::OpenFile(dest, "wb");
  if (!f) {
    return false;
  }
  size_t n = std::fwrite(bytes.data(), 1, bytes.size(), f);
  std::fclose(f);
  return n == bytes.size();
}

}  // namespace wx_ui
}  // namespace app
}  // namespace xe
