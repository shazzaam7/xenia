/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// Standalone game-disc reader for the library scanner. Reuses the exact
// parsers the emulator boots from (XEX optional headers, STFS headers, the
// VFS container devices, SPA title database) but only ever reads: nothing is
// mounted into the live VFS and no guest memory is touched.

#include "xenia/app/wx/wx_game_scan.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <span>

#include "third_party/crypto/TinySHA1.hpp"
#include "third_party/fmt/include/fmt/format.h"
#include "xenia/app/wx/wx_game_model.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/mapped_memory.h"
#include "xenia/base/math.h"
#include "xenia/base/pe_image.h"
#include "xenia/base/string.h"
#include "xenia/cpu/lzx.h"
#include "xenia/cpu/xex_module.h"
#include "xenia/kernel/util/xex2_info.h"
#include "xenia/kernel/xam/xcontent/xcontent.h"
#include "xenia/kernel/xam/xdbf/spa_info.h"
#include "xenia/vfs/devices/disc_image_device.h"
#include "xenia/vfs/devices/disc_zarchive_device.h"
#include "xenia/vfs/devices/xcontent_container_device.h"
#include "xenia/vfs/devices/xcontent_devices/stfs_container_device.h"
#include "xenia/vfs/devices/xcontent_devices/svod_container_device.h"
#include "xenia/vfs/entry.h"
#include "xenia/vfs/file.h"
#include "xenia/xbox.h"

// Session-key AES-CBC decryptor owned by xex_module.cc (same routine boot
// uses); linked from xenia-cpu.
void aes_decrypt_buffer(const uint8_t* session_key, const uint8_t* input_buffer,
                        const size_t input_size, uint8_t* output_buffer,
                        const size_t output_size);

namespace xe {
namespace app {
namespace wx_ui {

namespace {

// XEX image keys, same constants as xex_module.cc ReadImage key retry order.
constexpr uint8_t kXex2RetailKey[16] = {0x20, 0xB1, 0x85, 0xA5, 0x9D, 0x28,
                                        0xFD, 0xC3, 0x40, 0x58, 0x3F, 0xBB,
                                        0x08, 0x96, 0xBF, 0x91};
constexpr uint8_t kXex2DevkitKey[16] = {0};
constexpr uint8_t kXex1RetailKey[16] = {0xA2, 0x6C, 0x10, 0xF7, 0x1F, 0xD9,
                                        0x35, 0xE9, 0x8B, 0x99, 0x92, 0x2C,
                                        0xE9, 0x32, 0x15, 0x72};
constexpr uint8_t kXex1DevkitKey[16] = {0xA8, 0xB0, 0x05, 0x12, 0xED, 0xE3,
                                        0x63, 0x8D, 0xC6, 0x58, 0xB3, 0x10,
                                        0x1F, 0x9F, 0x50, 0xD1};

constexpr size_t kMaxImageBytes = 512ull * 1024 * 1024;

std::string ToHex8(uint32_t value) { return fmt::format("{:08X}", value); }

// "major.minor.build.qfe" from a packed XEX version value, the form used
// for release folder names and for showing a version to the user.
std::string FormatXexVersion(uint32_t value) {
  xex2_version version{value};
  // Bit-fields cannot bind to fmt's forwarding references; copy out first.
  const uint32_t major = version.major, minor = version.minor,
                 build = version.build, qfe = version.qfe;
  return fmt::format("{}.{}.{}.{}", major, minor, build, qfe);
}

bool HasPngOrJpegMagic(const uint8_t* data, size_t size) {
  if (size < 4) {
    return false;
  }
  static const uint8_t kPng[] = {0x89, 0x50, 0x4E, 0x47};
  static const uint8_t kJpeg[] = {0xFF, 0xD8, 0xFF};
  return std::memcmp(data, kPng, 4) == 0 || std::memcmp(data, kJpeg, 3) == 0;
}

std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return char(std::tolower(c)); });
  return s;
}

// ASCII case-insensitive compare (Manager OrdinalIgnoreCase): XEX resource
// and section names are 8-char uppercase hex title IDs, but homebrew or odd
// tooling may vary the case.
bool EqualXexName(const char* a, const char* b, size_t n) {
  for (size_t i = 0; i < n; i++) {
    unsigned char ca = static_cast<unsigned char>(a[i]);
    unsigned char cb = static_cast<unsigned char>(b[i]);
    if (ca >= 'a' && ca <= 'z') {
      ca -= 'a' - 'A';
    }
    if (cb >= 'a' && cb <= 'z') {
      cb -= 'a' - 'A';
    }
    if (ca != cb) {
      return false;
    }
  }
  return true;
}

bool ReadFilePrefix(const std::filesystem::path& path, size_t count,
                    std::vector<uint8_t>& out) {
  out.clear();
  FILE* f = xe::filesystem::OpenFile(path, "rb");
  if (!f) {
    return false;
  }
  out.resize(count);
  size_t n = std::fread(out.data(), 1, count, f);
  std::fclose(f);
  out.resize(n);
  return true;
}

bool ReadWholeFile(const std::filesystem::path& path,
                   std::vector<uint8_t>& out) {
  out.clear();
  std::error_code ec = {};
  uint64_t size = std::filesystem::file_size(path, ec);
  if (ec || size == 0 || size > kMaxImageBytes) {
    return false;
  }
  FILE* f = xe::filesystem::OpenFile(path, "rb");
  if (!f) {
    return false;
  }
  out.resize(size_t(size));
  size_t n = std::fread(out.data(), 1, out.size(), f);
  std::fclose(f);
  out.resize(n);
  return n == size_t(size);
}

bool IsGameContentType(XContentType type) {
  switch (type) {
    case XContentType::kXbox360Title:
    case XContentType::kGameDemo:
    case XContentType::kArcadeTitle:
    case XContentType::kInstalledGame:
    case XContentType::kGameTitle:
      return true;
    default:
      return false;
  }
}

const kernel::xam::XContentContainerHeader* AsContainerHeader(
    const std::vector<uint8_t>& bytes) {
  if (bytes.size() < sizeof(kernel::xam::XContentContainerHeader)) {
    return nullptr;
  }
  return reinterpret_cast<const kernel::xam::XContentContainerHeader*>(
      bytes.data());
}

void FillMetaFromStfsHeader(const kernel::xam::XContentContainerHeader* header,
                            GameMeta& meta) {
  const auto& md = header->content_metadata;
  meta.title_id = ToHex8(static_cast<uint32_t>(md.execution_info.title_id));
  meta.media_id = ToHex8(static_cast<uint32_t>(md.execution_info.media_id));
  meta.version =
      FormatXexVersion(static_cast<uint32_t>(md.execution_info.version_value));
  meta.disc_number =
      md.execution_info.disc_number ? int(md.execution_info.disc_number) : 1;
  meta.disc_count =
      md.execution_info.disc_count ? int(md.execution_info.disc_count) : 1;
  std::string name = xe::to_utf8(md.display_name(XLanguage::kEnglish));
  if (name.empty()) {
    name = xe::to_utf8(md.title_name());
  }
  meta.name = name;
  // Thumbnail first, title thumbnail second (Manager TryGetIcon order).
  const uint8_t* candidates[2] = {md.thumbnail, md.title_thumbnail};
  uint32_t sizes[2] = {static_cast<uint32_t>(md.thumbnail_size),
                       static_cast<uint32_t>(md.title_thumbnail_size)};
  for (int i = 0; i < 2; i++) {
    if (sizes[i] > 0 && sizes[i] <= sizeof(md.thumbnail) &&
        HasPngOrJpegMagic(candidates[i], sizes[i])) {
      meta.icon_bytes.assign(candidates[i], candidates[i] + sizes[i]);
      break;
    }
  }
}

bool ReadStfsHeader(const std::filesystem::path& path,
                    std::vector<uint8_t>& header_out) {
  // XContentContainerHeader = 0x344 + 0x93D6; round up.
  return ReadFilePrefix(path, 0xA000, header_out) &&
         header_out.size() >= sizeof(kernel::xam::XContentContainerHeader);
}

// The STFS volume starts at the first block boundary after the container
// header and every block offset is relative to it. The boot path
// (ContentPackageContainer::MountPackage) mounts exactly this slice, so the
// scanner must too - reading block offsets from the file start lands in the
// metadata region and yields an empty (or garbage) file table.
uint64_t ContainerDataOffset(
    const kernel::xam::XContentContainerHeader* header) {
  return uint64_t(
      xe::round_up(static_cast<uint32_t>(header->content_header.header_size),
                   vfs::XContentContainerDevice::kBlockSize));
}

// Locates the XContent header file of a package: the file itself, or the
// CON/LIVE/PIRS file inside a GOD-style directory (which holds a sibling
// "<header>.data" directory with the data fragments).
bool ResolvePackageHeaderFile(const std::filesystem::path& path,
                              std::filesystem::path& header_path,
                              size_t& data_file_count) {
  header_path = path;
  data_file_count = 1;
  std::error_code ec = {};
  if (std::filesystem::is_directory(path, ec)) {
    std::vector<std::filesystem::path> files;
    for (const auto& e : std::filesystem::directory_iterator(path, ec)) {
      if (ec) {
        break;
      }
      std::error_code ec2 = {};
      if (e.is_regular_file(ec2)) {
        files.push_back(e.path());
      }
    }
    if (files.empty()) {
      return false;
    }
    std::sort(files.begin(), files.end());
    header_path.clear();
    for (const auto& f : files) {
      std::vector<uint8_t> magic;
      if (!ReadFilePrefix(f, 4, magic) || magic.size() < 4) {
        continue;
      }
      fourcc_t sig = make_fourcc(magic[0], magic[1], magic[2], magic[3]);
      if (sig != vfs::kCONSignature && sig != vfs::kLIVESignature &&
          sig != vfs::kPIRSSignature) {
        continue;
      }
      std::vector<uint8_t> header;
      if (!ReadStfsHeader(f, header)) {
        continue;
      }
      const auto* h = AsContainerHeader(header);
      if (h && h->content_header.is_magic_valid()) {
        header_path = f;
        break;
      }
    }
    if (header_path.empty()) {
      header_path = files[0];
    }
  }
  // SVOD packages keep their payload in "<header>.data/Data000#".
  std::filesystem::path fragments = header_path;
  fragments += ".data";
  std::error_code dec = {};
  if (std::filesystem::is_directory(fragments, dec)) {
    size_t count = 0;
    for (const auto& e : std::filesystem::directory_iterator(fragments, dec)) {
      if (dec) {
        break;
      }
      if (e.is_regular_file()) {
        count++;
      }
    }
    if (count) {
      data_file_count = count;
    }
  }
  return true;
}

// Case-insensitive single-file read through a standalone device.
bool ReadDeviceFile(vfs::Device& device, const std::string& name,
                    std::vector<uint8_t>& out) {
  out.clear();
  vfs::Entry* entry = device.ResolvePath(name);
  if (!entry || entry->size() == 0 || entry->size() > kMaxImageBytes) {
    return false;
  }
  vfs::File* file = nullptr;
  if (entry->Open(vfs::FileAccess::kFileReadData, &file) != X_STATUS_SUCCESS ||
      !file) {
    return false;
  }
  out.resize(entry->size());
  size_t done = 0;
  while (done < out.size()) {
    size_t got = 0;
    std::span<uint8_t> span(out.data() + done, out.size() - done);
    if (file->ReadSync(span, done, &got) != X_STATUS_SUCCESS || got == 0) {
      break;
    }
    done += got;
  }
  file->Destroy();
  out.resize(done);
  return done == entry->size();
}

struct ContainerDevice {
  std::unique_ptr<vfs::Device> device;
  // Keeps STFS/SVOD header (and its embedded descriptor) alive.
  std::vector<uint8_t> header_bytes;
  // STFS mounts a slice of the package; the slice does not own the mapping,
  // so the full map has to outlive the device.
  std::unique_ptr<MappedMemory> package_map;
};

std::unique_ptr<ContainerDevice> OpenContainer(
    const std::filesystem::path& path, GameFileType type) {
  auto holder = std::make_unique<ContainerDevice>();
  switch (type) {
    case GameFileType::kIso: {
      auto device = std::make_unique<vfs::DiscImageDevice>("", path);
      if (!device->Initialize()) {
        return nullptr;
      }
      holder->device = std::move(device);
      return holder;
    }
    case GameFileType::kZar: {
      auto device = std::make_unique<vfs::DiscZarchiveDevice>("", path);
      if (!device->Initialize()) {
        return nullptr;
      }
      holder->device = std::move(device);
      return holder;
    }
    case GameFileType::kStfs:
    case GameFileType::kSvod: {
      std::filesystem::path header_path;
      size_t data_file_count = 1;
      if (!ResolvePackageHeaderFile(path, header_path, data_file_count)) {
        return nullptr;
      }
      if (!ReadStfsHeader(header_path, holder->header_bytes)) {
        return nullptr;
      }
      const auto* header = AsContainerHeader(holder->header_bytes);
      if (!header || !header->content_header.is_magic_valid()) {
        return nullptr;
      }
      if (type == GameFileType::kSvod) {
        if (header->content_metadata.data_file_count) {
          data_file_count = header->content_metadata.data_file_count;
        }
        auto device = std::make_unique<vfs::SvodContainerDevice>(
            "", header_path, data_file_count,
            &header->content_metadata.volume_descriptor.svod);
        if (!device->Initialize()) {
          return nullptr;
        }
        holder->device = std::move(device);
      } else {
        auto mmap = MappedMemory::Open(header_path, MappedMemory::Mode::kRead);
        if (!mmap) {
          return nullptr;
        }
        const uint64_t data_offset = ContainerDataOffset(header);
        if (data_offset >= mmap->size()) {
          return nullptr;
        }
        // Non-owning slice; holder->package_map keeps the map alive.
        auto data = mmap->Slice(size_t(data_offset),
                                size_t(mmap->size() - data_offset));
        holder->package_map = std::move(mmap);
        // Device only reads the descriptor; our header buffer is mutable and
        // outlives the device.
        auto* descriptor = const_cast<vfs::StfsVolumeDescriptor*>(
            &header->content_metadata.volume_descriptor.stfs);
        auto device = std::make_unique<vfs::StfsContainerDevice>(
            "", descriptor, std::move(data));
        if (!device->Initialize()) {
          return nullptr;
        }
        holder->device = std::move(device);
      }
      return holder;
    }
    default:
      return nullptr;
  }
}

}  // namespace

GameFileType IdentifyFile(const std::filesystem::path& path) {
  std::error_code ec = {};
  if (std::filesystem::is_directory(path, ec)) {
    // Extracted title, GOD-split package or a folder holding a single
    // container file: probe top-level files.
    bool has_default_xex = false;
    GameFileType container_type = GameFileType::kUnknown;
    bool has_data_files = false;
    for (const auto& e : std::filesystem::directory_iterator(path, ec)) {
      std::error_code ec2 = {};
      if (!e.is_regular_file(ec2)) {
        continue;
      }
      std::string name = Lower(e.path().filename().string());
      if (name == "default.xex") {
        has_default_xex = true;
      }
      // GOD data fragments are named Data0000... next to the header file.
      if (name.size() > 4 && name.rfind("data", 0) == 0 &&
          std::all_of(name.begin() + 4, name.end(),
                      [](unsigned char c) { return std::isdigit(c) != 0; })) {
        has_data_files = true;
      }
      if (name.ends_with(".xex")) {
        continue;
      }
      std::vector<uint8_t> magic;
      if (!ReadFilePrefix(e.path(), 4, magic) || magic.size() < 4) {
        continue;
      }
      fourcc_t sig = make_fourcc(magic[0], magic[1], magic[2], magic[3]);
      if (sig != vfs::kCONSignature && sig != vfs::kLIVESignature &&
          sig != vfs::kPIRSSignature) {
        continue;
      }
      // Only a package the emulator can boot claims the folder: many signed
      // files (nxeart, avatar awards, title updates, live data) share the
      // magic but are not games.
      std::vector<uint8_t> header;
      if (!ReadStfsHeader(e.path(), header)) {
        continue;
      }
      const auto* container = AsContainerHeader(header);
      if (!container || !container->content_header.is_magic_valid() ||
          !IsGameContentType(static_cast<XContentType>(
              container->content_metadata.content_type))) {
        continue;
      }
      if (container->content_metadata.volume_type ==
          kernel::xam::XContentVolumeType::kSvod) {
        container_type = GameFileType::kSvod;
      } else if (container_type == GameFileType::kUnknown) {
        container_type = GameFileType::kStfs;
      }
    }
    if (!has_default_xex && container_type == GameFileType::kUnknown) {
      // Unusual extracted layouts keep default.xex below the top level
      // (Manager searches recursively). Filename probe only, no magic reads.
      std::error_code rec = {};
      for (auto it = std::filesystem::recursive_directory_iterator(path, rec);
           it != std::filesystem::recursive_directory_iterator(); ++it) {
        if (rec) {
          break;
        }
        std::error_code ec3 = {};
        if (it->is_directory(ec3)) {
          if (Lower(it->path().filename().string()) == "$systemupdate") {
            it.disable_recursion_pending();
          }
          continue;
        }
        if (!it->is_regular_file(ec3)) {
          continue;
        }
        if (Lower(it->path().filename().string()) == "default.xex") {
          has_default_xex = true;
          break;
        }
      }
    }
    if (has_default_xex) {
      return GameFileType::kXex;
    }
    // A folder holding a bootable container is that container, not an
    // extracted title (this used to report kXex and then fail to find any
    // XEX, so the game was skipped).
    if (container_type != GameFileType::kUnknown) {
      return container_type;
    }
    if (has_data_files) {
      return GameFileType::kSvod;
    }
    return GameFileType::kUnknown;
  }
  std::vector<uint8_t> magic;
  if (!ReadFilePrefix(path, 4, magic) || magic.size() < 4) {
    return GameFileType::kUnknown;
  }
  fourcc_t sig = make_fourcc(magic[0], magic[1], magic[2], magic[3]);
  if (sig == cpu::kXEX1Signature || sig == cpu::kXEX2Signature ||
      sig == cpu::kXEX25Signature) {
    return GameFileType::kXex;
  }
  if (sig == vfs::kCONSignature || sig == vfs::kLIVESignature ||
      sig == vfs::kPIRSSignature) {
    std::vector<uint8_t> header;
    if (!ReadStfsHeader(path, header)) {
      return GameFileType::kUnknown;
    }
    const auto* container = AsContainerHeader(header);
    if (!container) {
      return GameFileType::kUnknown;
    }
    return container->content_metadata.volume_type ==
                   kernel::xam::XContentVolumeType::kSvod
               ? GameFileType::kSvod
               : GameFileType::kStfs;
  }
  if (sig == vfs::kXSFSignature) {
    return GameFileType::kIso;
  }
  // ZAR magic sits in the file footer; missing XSF header may still be XISO.
  uint64_t size = std::filesystem::file_size(path, ec);
  if (!ec && size >= 4) {
    FILE* f = xe::filesystem::OpenFile(path, "rb");
    if (f) {
      char footer[4] = {};
      xe::filesystem::Seek(f, -4, SEEK_END);
      std::fread(footer, 1, 4, f);
      std::fclose(f);
      if (make_fourcc(footer[0], footer[1], footer[2], footer[3]) ==
          vfs::kZarMagic) {
        return GameFileType::kZar;
      }
    }
  }
  auto iso = std::make_unique<vfs::DiscImageDevice>("", path);
  if (iso->Initialize()) {
    return GameFileType::kIso;
  }
  return GameFileType::kUnknown;
}

bool ReadXexMeta(const uint8_t* data, size_t size, GameMeta& meta_out) {
  if (size < sizeof(xex2_header)) {
    return false;
  }
  auto header = reinterpret_cast<const xex2_header*>(data);
  if (header->magic != cpu::kXEX1Signature &&
      header->magic != cpu::kXEX2Signature &&
      header->magic != cpu::kXEX25Signature) {
    return false;
  }
  if (!header->header_size || header->header_size > size) {
    return false;
  }
  if (header->header_size < 0x18 + header->header_count * 8u) {
    return false;
  }
  xex2_opt_execution_info* exec = nullptr;
  if (!cpu::XexModule::GetOptHeader(header, XEX_HEADER_EXECUTION_INFO, &exec) ||
      !exec) {
    return false;
  }
  meta_out.type = GameFileType::kXex;
  meta_out.title_id = ToHex8(static_cast<uint32_t>(exec->title_id));
  meta_out.media_id = ToHex8(static_cast<uint32_t>(exec->media_id));
  meta_out.version =
      FormatXexVersion(static_cast<uint32_t>(exec->version_value));
  meta_out.disc_number = exec->disc_number ? int(exec->disc_number) : 1;
  meta_out.disc_count = exec->disc_count ? int(exec->disc_count) : 1;
  if (meta_out.title_id == "00000000") {
    return false;
  }
  return true;
}

namespace {

// Host-side port of XexModule::ReadImage* writing into a malloc'd buffer
// instead of guest memory.
bool DecryptXexImage(const uint8_t* file, size_t file_size,
                     const xex2_header* header, std::vector<uint8_t>& out) {
  out.clear();
  xex2_opt_file_format_info* format = nullptr;
  if (!cpu::XexModule::GetOptHeader(header, XEX_HEADER_FILE_FORMAT_INFO,
                                    &format) ||
      !format) {
    return false;
  }
  if (static_cast<uint32_t>(header->module_flags) &
      (XEX_MODULE_MODULE_PATCH | XEX_MODULE_PATCH_FULL |
       XEX_MODULE_PATCH_DELTA)) {
    return false;  // Title updates carry no SPA of their own.
  }
  const uint8_t* keys[4] = {kXex2RetailKey, kXex2DevkitKey, kXex1RetailKey,
                            kXex1DevkitKey};
  const void* sec_ptr = cpu::XexModule::GetSecurityInfo(header);
  if (!sec_ptr) {
    return false;
  }
  size_t sec_off = static_cast<const uint8_t*>(sec_ptr) - file;
  if (sec_off + sizeof(uint32_t) > static_cast<uint32_t>(header->header_size)) {
    return false;
  }
  // Authoritative image size from the security header (+4 on all XEX
  // formats). The page-descriptor sum below agrees with it; prefer the larger
  // of the two when sane — Basic compression omits trailing zero blocks, so
  // the block sum can be a few KB short of the true image (and of resource
  // data at its tail).
  const uint8_t* sec_bytes = static_cast<const uint8_t*>(sec_ptr);
  uint32_t sec_image_size =
      (uint32_t(sec_bytes[4]) << 24) | (uint32_t(sec_bytes[5]) << 16) |
      (uint32_t(sec_bytes[6]) << 8) | uint32_t(sec_bytes[7]);
  if (sec_image_size == 0 || sec_image_size > kMaxImageBytes) {
    sec_image_size = 0;
  }
  // Security structs share field names; resolve per format like
  // XexModule::ReadSecurityInfo.
  const uint8_t* aes_key = nullptr;
  const xex2_page_descriptor* descriptors = nullptr;
  uint32_t descriptor_count = 0;
  uint32_t load_address = 0;
  if (header->magic == cpu::kXEX25Signature) {
    auto s = reinterpret_cast<const xex25_security_info*>(sec_ptr);
    aes_key = reinterpret_cast<const uint8_t*>(s->aes_key);
    descriptors = s->page_descriptors;
    descriptor_count = static_cast<uint32_t>(s->page_descriptor_count);
    load_address = static_cast<uint32_t>(s->load_address);
  } else if (header->magic == cpu::kXEX1Signature) {
    auto s = reinterpret_cast<const xex1_security_info*>(sec_ptr);
    aes_key = reinterpret_cast<const uint8_t*>(s->aes_key);
    descriptors = s->page_descriptors;
    descriptor_count = static_cast<uint32_t>(s->page_descriptor_count);
    load_address = static_cast<uint32_t>(s->load_address);
  } else {
    auto s = reinterpret_cast<const xex2_security_info*>(sec_ptr);
    aes_key = reinterpret_cast<const uint8_t*>(s->aes_key);
    descriptors = s->page_descriptors;
    descriptor_count = static_cast<uint32_t>(s->page_descriptor_count);
    load_address = static_cast<uint32_t>(s->load_address);
  }
  // Image pages are 64K below 0x90000000, 4K above — same rule boot uses to
  // size the image (a 4K assumption starves LZX output 16x and every retail
  // ISO loses its icon).
  uint32_t base_address = load_address;
  xe::be<uint32_t>* base_opt = nullptr;
  if (cpu::XexModule::GetOptHeader(header, XEX_HEADER_IMAGE_BASE_ADDRESS,
                                   &base_opt) &&
      base_opt) {
    base_address = static_cast<uint32_t>(*base_opt);
  }
  const uint64_t xex_page_size =
      base_address <= 0x90000000 ? 64ull * 1024 : 4ull * 1024;
  uint64_t image_pages = 0;
  // Descriptors must lie inside the header.
  if (sec_off + 8 + uint64_t(descriptor_count) * sizeof(xex2_page_descriptor) >
      static_cast<uint32_t>(header->header_size)) {
    return false;
  }
  for (uint32_t i = 0; i < descriptor_count; i++) {
    // NOTE: no xe::byte_swap here — be<> already converts to host order via
    // the cast, and byte_swap on a be<> double-swaps (every retail ISO/XEX
    // loses its icon/title that way).
    image_pages += static_cast<uint32_t>(descriptors[i].value) >> 4;
  }
  uint64_t image_size = image_pages * xex_page_size;
  if (sec_image_size > image_size) {
    image_size = sec_image_size;
  }
  if (!image_size || image_size > kMaxImageBytes) {
    return false;
  }
  const uint8_t* image = file + static_cast<uint32_t>(header->header_size);
  size_t image_len = file_size - static_cast<uint32_t>(header->header_size);
  // Format info (with its compression payload description) must lie inside
  // the header.
  if (reinterpret_cast<const uint8_t*>(format) + sizeof(*format) >
          file + static_cast<uint32_t>(header->header_size) ||
      reinterpret_cast<const uint8_t*>(format) +
              static_cast<uint32_t>(format->info_size) >
          file + static_cast<uint32_t>(header->header_size)) {
    return false;
  }
  uint32_t encryption = static_cast<uint32_t>(format->encryption_type);
  uint32_t compression = static_cast<uint32_t>(format->compression_type);

  for (int k = 0; k < 4; k++) {
    uint8_t session[0x10] = {};
    aes_decrypt_buffer(keys[k], aes_key, 16, session, 16);
    std::vector<uint8_t> plain;
    bool step_ok = false;
    if (compression == XEX_COMPRESSION_NONE) {
      if (image_len > kMaxImageBytes) {
        return false;
      }
      plain.resize(image_len);
      if (encryption == XEX_ENCRYPTION_NONE) {
        std::memcpy(plain.data(), image, image_len);
        step_ok = true;
      } else if (encryption == XEX_ENCRYPTION_NORMAL) {
        aes_decrypt_buffer(session, image, image_len, plain.data(), image_len);
        step_ok = true;
      }
    } else if (compression == XEX_COMPRESSION_BASIC) {
      uint32_t blocks = (static_cast<uint32_t>(format->info_size) - 8) / 8;
      uint64_t total = 0;
      auto& comp = format->compression_info.basic;
      for (uint32_t n = 0; n < blocks; n++) {
        total += static_cast<uint32_t>(comp.blocks[n].data_size) +
                 static_cast<uint32_t>(comp.blocks[n].zero_size);
      }
      if (!total || total > kMaxImageBytes) {
        return false;
      }
      // Pad to the security-header image size when larger: trailing zero
      // blocks are omitted from the stored stream but resource data (SPA)
      // may extend into them.
      uint64_t out_size = total;
      if (sec_image_size > out_size) {
        out_size = sec_image_size;
      }
      plain.assign(size_t(out_size), 0);
      const uint8_t* p = image;
      uint8_t* d = plain.data();
      step_ok = true;
      if (encryption == XEX_ENCRYPTION_NORMAL) {
        // Re-key per attempt through aes_decrypt_buffer's chaining is not
        // possible (fresh IV each call), so chain manually here is skipped:
        // BASIC images are rare; decrypt contiguously instead.
        std::vector<uint8_t> cat;
        uint64_t cat_len = 0;
        for (uint32_t n = 0; n < blocks; n++) {
          cat_len += static_cast<uint32_t>(comp.blocks[n].data_size);
        }
        if (cat_len > image_len) {
          return false;
        }
        cat.resize(size_t(cat_len));
        aes_decrypt_buffer(session, image, size_t(cat_len), cat.data(),
                           size_t(cat_len));
        const uint8_t* cp = cat.data();
        for (uint32_t n = 0; n < blocks; n++) {
          uint32_t ds = static_cast<uint32_t>(comp.blocks[n].data_size);
          uint32_t zs = static_cast<uint32_t>(comp.blocks[n].zero_size);
          std::memcpy(d, cp, ds);
          cp += ds;
          d += ds + zs;
        }
      } else if (encryption == XEX_ENCRYPTION_NONE) {
        for (uint32_t n = 0; n < blocks; n++) {
          uint32_t ds = static_cast<uint32_t>(comp.blocks[n].data_size);
          uint32_t zs = static_cast<uint32_t>(comp.blocks[n].zero_size);
          if (p + ds > image + image_len) {
            step_ok = false;
            break;
          }
          std::memcpy(d, p, ds);
          p += ds;
          d += ds + zs;
        }
      } else {
        step_ok = false;
      }
    } else if (compression == XEX_COMPRESSION_NORMAL) {
      std::vector<uint8_t> blocked;
      if (encryption == XEX_ENCRYPTION_NONE) {
        blocked.assign(image, image + image_len);
      } else if (encryption == XEX_ENCRYPTION_NORMAL) {
        blocked.resize(image_len);
        aes_decrypt_buffer(session, image, image_len, blocked.data(),
                           image_len);
      } else {
        continue;
      }
      // De-block with hash check, like ReadImageCompressed.
      std::vector<uint8_t> cat;
      cat.reserve(image_len);
      const uint8_t* p = blocked.data();
      const uint8_t* end = p + blocked.size();
      const xex2_compressed_block_info* cur =
          &format->compression_info.normal.first_block;
      sha1::SHA1 s;
      uint8_t digest[0x14];
      step_ok = true;
      while (cur->block_size) {
        uint32_t bsize = static_cast<uint32_t>(cur->block_size);
        if (p + bsize > end) {
          step_ok = false;
          break;
        }
        s.reset();
        s.processBytes(p, bsize);
        s.finalize(digest);
        if (std::memcmp(digest, cur->block_hash, 0x14) != 0) {
          step_ok = false;  // Wrong key; try next.
          break;
        }
        // Next block descriptor is stored at the start of the current block
        // (first 24 bytes of the hashed region), not at pnext.
        const auto* next_block =
            reinterpret_cast<const xex2_compressed_block_info*>(p);
        const uint8_t* q = p + 4 + 20;
        const uint8_t* qend = p + bsize;
        while (q + 2 <= qend) {
          size_t chunk = (size_t(q[0]) << 8) | q[1];
          q += 2;
          if (!chunk) {
            break;
          }
          if (q + chunk > qend) {
            step_ok = false;
            break;
          }
          cat.insert(cat.end(), q, q + chunk);
          q += chunk;
        }
        if (!step_ok) {
          break;
        }
        const uint8_t* next_p = p + bsize;
        p = next_p;
        cur = next_block;
      }
      if (step_ok) {
        plain.assign(size_t(image_size), 0);
        uint32_t window =
            static_cast<uint32_t>(format->compression_info.normal.window_size);
        if (lzx_decompress(cat.data(), cat.size(), plain.data(), plain.size(),
                           window, nullptr, 0) != 0) {
          step_ok = false;
        }
      }
    } else {
      return false;  // Delta patches and unknown schemes carry no SPA.
    }
    if (!step_ok || plain.size() < sizeof(XIMAGE_DOS_HEADER)) {
      continue;
    }
    auto dos = reinterpret_cast<const XIMAGE_DOS_HEADER*>(plain.data());
    if (dos->e_magic != XIMAGE_DOS_SIGNATURE) {
      continue;  // Wrong key; try next.
    }
    out = std::move(plain);
    return true;
  }
  return false;
}

struct PeSection {
  std::string name;
  uint32_t vaddr = 0;  // ImageBase + VirtualAddress.
  uint32_t vsize = 0;
  uint32_t raw = 0;
  uint32_t raw_size = 0;
};

bool WalkPeSections(const std::vector<uint8_t>& image, uint32_t& base_out,
                    std::vector<PeSection>& sections_out) {
  sections_out.clear();
  if (image.size() < sizeof(XIMAGE_DOS_HEADER)) {
    return false;
  }
  auto dos = reinterpret_cast<const XIMAGE_DOS_HEADER*>(image.data());
  if (dos->e_magic != XIMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
    return false;
  }
  size_t nt_off = size_t(dos->e_lfanew);
  if (nt_off + sizeof(XIMAGE_NT_HEADERS32) > image.size()) {
    return false;
  }
  auto nt = reinterpret_cast<const XIMAGE_NT_HEADERS32*>(image.data() + nt_off);
  if (nt->Signature != XIMAGE_NT_SIGNATURE ||
      nt->FileHeader.Machine != XIMAGE_FILE_MACHINE_POWERPCBE ||
      !(nt->FileHeader.Characteristics & XIMAGE_FILE_32BIT_MACHINE) ||
      nt->FileHeader.SizeOfOptionalHeader != XIMAGE_SIZEOF_NT_OPTIONAL_HEADER ||
      nt->OptionalHeader.Magic != XIMAGE_NT_OPTIONAL_HDR32_MAGIC ||
      nt->OptionalHeader.Subsystem != XIMAGE_SUBSYSTEM_XBOX) {
    return false;
  }
  base_out = nt->OptionalHeader.ImageBase;
  if (nt->FileHeader.NumberOfSections > 96) {
    return false;
  }
  auto sechdr = XIMAGE_FIRST_SECTION(nt);
  for (int n = 0; n < nt->FileHeader.NumberOfSections; n++, sechdr++) {
    if (reinterpret_cast<const uint8_t*>(sechdr + 1) >
        image.data() + image.size()) {
      return false;
    }
    PeSection s;
    char name[9] = {};
    std::memcpy(name, sechdr->Name, 8);
    s.name = name;
    s.vaddr = base_out + sechdr->VirtualAddress;
    s.vsize = sechdr->Misc.VirtualSize;
    s.raw = sechdr->PointerToRawData;
    s.raw_size = sechdr->SizeOfRawData;
    sections_out.push_back(s);
  }
  return !sections_out.empty();
}

bool LooksLikeSpa(const uint8_t* data, size_t size) {
  return size > 4 && std::memcmp(data, "XDBF", 4) == 0;
}

int FindPngEnd(const uint8_t* data, size_t size, size_t start) {
  if (start + 8 > size || data[start] != 0x89) {
    return -1;
  }
  size_t pos = start + 8;
  while (pos + 12 <= size) {
    uint32_t len = (uint32_t(data[pos]) << 24) |
                   (uint32_t(data[pos + 1]) << 16) |
                   (uint32_t(data[pos + 2]) << 8) | uint32_t(data[pos + 3]);
    if (pos + 8 + len + 4 > size) {
      break;
    }
    bool is_iend = data[pos + 4] == 'I' && data[pos + 5] == 'E' &&
                   data[pos + 6] == 'N' && data[pos + 7] == 'D';
    pos += 8 + len + 4;
    if (is_iend) {
      return int(pos);
    }
    if (pos - start > 2'000'000) {
      break;
    }
  }
  return -1;
}

std::vector<uint8_t> FindLargestPngInBytes(const uint8_t* data, size_t size) {
  std::vector<uint8_t> best;
  for (size_t i = 0; i + 8 < size; ++i) {
    if (data[i] == 0x89 && data[i + 1] == 0x50 && data[i + 2] == 0x4E &&
        data[i + 3] == 0x47) {
      int end = FindPngEnd(data, size, i);
      if (end > int(i + 67)) {
        size_t len = size_t(end - int(i));
        if (len >= 1024 && len > best.size()) {
          best.assign(data + i, data + end);
        }
      }
    }
  }
  return best;
}

}  // namespace

bool ExtractXexSpa(const std::vector<uint8_t>& xex_bytes,
                   const std::string& title_id, std::vector<uint8_t>& spa_out) {
  spa_out.clear();
  if (xex_bytes.size() < sizeof(xex2_header) || title_id.size() != 8) {
    return false;
  }
  auto header = reinterpret_cast<const xex2_header*>(xex_bytes.data());
  if (!header->header_size || size_t(header->header_size) > xex_bytes.size()) {
    return false;
  }
  std::vector<uint8_t> image;
  if (!DecryptXexImage(xex_bytes.data(), xex_bytes.size(), header, image)) {
    return false;
  }
  uint32_t image_base = 0;
  std::vector<PeSection> sections;
  if (!WalkPeSections(image, image_base, sections)) {
    return false;
  }
  char want[8] = {};
  std::memcpy(want, title_id.data(), 8);
  // The decrypted image is the loaded image at image_base (contiguous, the
  // way boot lays blocks down in guest memory), so a resource address maps
  // directly off the base. It must NOT be gated on PE section containment:
  // resources can live in inter-section gaps the section table doesn't
  // describe (e.g. Forza 4's SPA at 0x82EA5780 sits between .XEXID/.edata).
  auto slice = [&](uint32_t vaddr, uint32_t size) -> bool {
    if (vaddr < image_base) {
      return false;
    }
    uint64_t raw = uint64_t(vaddr - image_base);
    if (raw + size > image.size() || !LooksLikeSpa(image.data() + raw, size)) {
      return false;
    }
    spa_out.assign(image.data() + raw, image.data() + raw + size);
    return true;
  };
  // Exact path first: RESOURCE_INFO entry, like UserModule::GetSection.
  xex2_opt_resource_info* resources = nullptr;
  if (cpu::XexModule::GetOptHeader(header, XEX_HEADER_RESOURCE_INFO,
                                   &resources) &&
      resources) {
    uint32_t count =
        (static_cast<uint32_t>(resources->size) - 4) / sizeof(xex2_resource);
    for (uint32_t i = 0; i < count; i++) {
      if (EqualXexName(resources->resources[i].name, want, 8)) {
        if (slice(static_cast<uint32_t>(resources->resources[i].address),
                  static_cast<uint32_t>(resources->resources[i].size))) {
          return true;
        }
        break;
      }
    }
  }
  // Fallback: PE section carrying the title ID name.
  for (const auto& s : sections) {
    if (s.name.size() >= 8 && EqualXexName(s.name.data(), want, 8) &&
        s.raw < image.size()) {
      uint32_t take =
          std::min({s.vsize, s.raw_size, uint32_t(image.size() - s.raw)});
      if (s.raw + take <= image.size() &&
          LooksLikeSpa(image.data() + s.raw, take)) {
        spa_out.assign(image.data() + s.raw, image.data() + s.raw + take);
        return true;
      }
    }
  }
  // Last resort: scan the decrypted image for any XDBF (Manager
  // ScanForXdbf). Handles stripped/homebrew where the title section name
  // is missing.
  for (size_t off = 0; off + 24 < image.size(); ++off) {
    if (std::memcmp(image.data() + off, "XDBF", 4) != 0) {
      continue;
    }
    uint32_t ver = (uint32_t(image[off + 4]) << 24) |
                   (uint32_t(image[off + 5]) << 16) |
                   (uint32_t(image[off + 6]) << 8) | uint32_t(image[off + 7]);
    if (ver != 0x00010000) {
      continue;
    }
    std::span<uint8_t> span(const_cast<uint8_t*>(image.data() + off),
                            image.size() - off);
    kernel::xam::SpaInfo probe(span);
    probe.Load();
    if (!probe.title_name().empty() || !probe.title_icon().empty()) {
      spa_out.assign(image.data() + off, image.data() + image.size());
      return true;
    }
  }
  return false;
}

bool ReadContainerFile(const std::filesystem::path& container_path,
                       GameFileType container_type, const std::string& name,
                       std::vector<uint8_t>& bytes_out) {
  auto holder = OpenContainer(container_path, container_type);
  if (!holder) {
    return false;
  }
  if (ReadDeviceFile(*holder->device, name, bytes_out)) {
    return true;
  }
  // Fallback: search recursively (handles default.xex / nxeart tucked inside
  // a subfolder, which some discs do). Bounded: a damaged package can expose
  // a bogus/looping entry tree.
  std::string target = Lower(name);
  vfs::Entry* root = holder->device->ResolvePath("");
  if (!root) {
    return false;
  }
  constexpr size_t kMaxEntriesVisited = 65536;
  size_t visited = 0;
  std::vector<vfs::Entry*> stack = {root};
  while (!stack.empty() && visited < kMaxEntriesVisited) {
    vfs::Entry* cur = stack.back();
    stack.pop_back();
    for (auto& child : cur->children()) {
      if (++visited > kMaxEntriesVisited) {
        return false;
      }
      std::string lower = Lower(child->name());
      if (lower == target) {
        // Use the child's full guest path for the device to resolve.
        if (ReadDeviceFile(*holder->device, child->path(), bytes_out)) {
          return true;
        }
      }
      if (child->attributes() & vfs::kFileAttributeDirectory) {
        stack.push_back(child.get());
      }
    }
  }
  return false;
}

bool ReadStfsMemoryFile(const uint8_t* stfs_bytes, size_t size,
                        const std::string& name,
                        std::vector<uint8_t>& bytes_out) {
  bytes_out.clear();
  if (!stfs_bytes || size < sizeof(kernel::xam::XContentContainerHeader)) {
    return false;
  }
  std::vector<uint8_t> copy(stfs_bytes, stfs_bytes + size);
  auto* header =
      reinterpret_cast<kernel::xam::XContentContainerHeader*>(copy.data());
  if (!header->content_header.is_magic_valid()) {
    return false;
  }
  const uint64_t data_offset = ContainerDataOffset(header);
  if (data_offset >= copy.size()) {
    return false;
  }
  // Non-owning wrappers; copy outlives the device below.
  auto* data = copy.data() + data_offset;
  auto mmap = std::unique_ptr<MappedMemory>(
      new MappedMemory(data, size_t(copy.size() - data_offset)));
  auto device = std::make_unique<vfs::StfsContainerDevice>(
      "", &header->content_metadata.volume_descriptor.stfs, std::move(mmap));
  if (!device->Initialize()) {
    return false;
  }
  if (ReadDeviceFile(*device, name, bytes_out)) {
    return true;
  }
  // Fallback: raw scan for JPEGs inside the STFS package. nxeart is known to
  // contain at most 2 JPEGs: nxebg.jpg (large, ~300KB) and nxeslot.jpg
  // (small, ~50KB). Scan for all FF D8 FF .. FF D9 and pick by size.
  std::string lower = Lower(name);
  if (lower == "nxebg.jpg" || lower == "nxeslot.jpg") {
    struct Jpeg {
      size_t off;
      size_t len;
    };
    std::vector<Jpeg> jpegs;
    for (size_t i = 0; i + 3 < size;) {
      if (stfs_bytes[i] == 0xFF && stfs_bytes[i + 1] == 0xD8 &&
          stfs_bytes[i + 2] == 0xFF) {
        size_t j = i + 3;
        while (j + 1 < size) {
          if (stfs_bytes[j] == 0xFF && stfs_bytes[j + 1] == 0xD9) {
            jpegs.push_back({i, j + 2 - i});
            i = j + 2;
            break;
          }
          ++j;
        }
        if (j + 1 >= size) {
          break;
        }
      } else {
        ++i;
      }
    }
    if (!jpegs.empty()) {
      std::sort(jpegs.begin(), jpegs.end(),
                [](const Jpeg& a, const Jpeg& b) { return a.len > b.len; });
      size_t pick = 0;
      if (lower == "nxeslot.jpg" && jpegs.size() > 1) {
        pick = 1;
      }
      if (pick < jpegs.size() && jpegs[pick].len >= 1024) {
        bytes_out.assign(stfs_bytes + jpegs[pick].off,
                         stfs_bytes + jpegs[pick].off + jpegs[pick].len);
        return true;
      }
    }
  }
  return false;
}

std::vector<std::string> ListContainerFiles(
    const std::filesystem::path& container_path, GameFileType container_type) {
  std::vector<std::string> names;
  auto holder = OpenContainer(container_path, container_type);
  if (!holder) {
    return names;
  }
  vfs::Entry* root = holder->device->ResolvePath("");
  if (!root) {
    return names;
  }
  for (const auto& child : root->children()) {
    names.push_back(child->name());
  }
  return names;
}

namespace {

bool FillMetaFromXexBytes(const std::vector<uint8_t>& xex_bytes,
                          const std::string& fallback_name, GameMeta& meta) {
  GameMeta m;
  if (!ReadXexMeta(xex_bytes.data(), xex_bytes.size(), m)) {
    return false;
  }
  // meta.type is deliberately left alone: for an ISO/ZAR this is reached
  // through the container, and reporting kXex there sends the artwork lookup
  // down the loose-XEX path (hunting for an nxeart next to, or anywhere under,
  // the disc's parent folder - which is some other game entirely).
  meta.title_id = m.title_id;
  meta.media_id = m.media_id;
  meta.version = m.version;
  meta.disc_number = m.disc_number;
  meta.disc_count = m.disc_count;
  std::vector<uint8_t> spa;
  if (ExtractXexSpa(xex_bytes, meta.title_id, spa)) {
    kernel::xam::SpaInfo info(std::span<uint8_t>(spa.data(), spa.size()));
    // Load() populates the language string tables title_name() reads; the
    // ctor alone leaves them empty (title would always fall back to the
    // file name).
    info.Load();
    meta.name = info.title_name();
    auto icon = info.title_icon();
    if (!icon.empty()) {
      meta.icon_bytes.assign(icon.begin(), icon.end());
    } else {
      // Fallback: largest valid PNG inside the SPA (Manager GetAnyValidIcon
      // / ScanForPng). Handles titles where 0x8000 is missing.
      auto png = FindLargestPngInBytes(spa.data(), spa.size());
      if (!png.empty()) {
        meta.icon_bytes = std::move(png);
      }
    }
  } else {
    // Title and icon fall back to the file name / placeholder below. Logged
    // (not silent) so a missing icon is diagnosable from xenia.log.
    XELOGW("Library: no title data (SPA) in XEX for {} ({}); using file name.",
           meta.title_id, fallback_name);
  }
  if (meta.name.empty()) {
    meta.name = fallback_name;
  }
  meta.ok = true;
  return true;
}

std::string StemName(const std::filesystem::path& path) {
  std::string stem = xe::path_to_utf8(path.stem());
  return stem.empty() ? xe::path_to_utf8(path.filename()) : stem;
}

// Extracted title layout: top-level default.xex first, then a nested
// default.xex (Manager's recursive LooseGameFileSource handles unusual
// layouts), then any top-level XEX, then any nested XEX. $systemupdate is
// never descended into.
std::filesystem::path FindExtractedXex(const std::filesystem::path& dir) {
  std::error_code ec = {};
  std::filesystem::path top_any;
  for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
    if (ec) {
      break;
    }
    std::error_code ec2 = {};
    if (!e.is_regular_file(ec2)) {
      continue;
    }
    std::string lower = Lower(e.path().filename().string());
    if (lower == "default.xex") {
      return e.path();
    }
    if (top_any.empty() && lower.ends_with(".xex")) {
      top_any = e.path();
    }
  }
  std::filesystem::path nested_any;
  std::error_code rec = {};
  for (auto it = std::filesystem::recursive_directory_iterator(dir, rec);
       it != std::filesystem::recursive_directory_iterator(); ++it) {
    if (rec) {
      break;
    }
    std::error_code ec3 = {};
    if (it->is_directory(ec3)) {
      if (Lower(it->path().filename().string()) == "$systemupdate") {
        it.disable_recursion_pending();
      }
      continue;
    }
    if (!it->is_regular_file(ec3)) {
      continue;
    }
    std::string lower = Lower(it->path().filename().string());
    if (lower == "default.xex") {
      return it->path();
    }
    if (top_any.empty() && nested_any.empty() && lower.ends_with(".xex")) {
      nested_any = it->path();
    }
  }
  if (!top_any.empty()) {
    return top_any;
  }
  return nested_any;
}

bool FillMetaFromContainerDefaultXex(const std::filesystem::path& path,
                                     GameFileType type, GameMeta& meta) {
  std::vector<uint8_t> xex;
  if (!ReadContainerFile(path, type, "default.xex", xex)) {
    // Fall back to any XEX anywhere in the image, preferring default.xex
    // (Manager searches the whole GDFX/ZAR tree).
    std::string pick;
    std::string pick_path;
    auto holder = OpenContainer(path, type);
    if (holder) {
      vfs::Entry* root = holder->device->ResolvePath("");
      if (root) {
        constexpr size_t kMaxEntriesVisited = 65536;
        size_t visited = 0;
        std::vector<vfs::Entry*> stack = {root};
        while (!stack.empty() && visited < kMaxEntriesVisited) {
          vfs::Entry* cur = stack.back();
          stack.pop_back();
          visited += cur->children().size();
          for (auto& child : cur->children()) {
            std::string lower = Lower(child->name());
            if (lower.ends_with(".xex")) {
              if (pick.empty() || lower == "default.xex") {
                pick = child->name();
                pick_path = child->path();
                if (lower == "default.xex") {
                  // Best candidate found; but still need to finish? break
                  // early for default.xex.
                }
              }
            }
            if (child->attributes() & vfs::kFileAttributeDirectory) {
              stack.push_back(child.get());
            }
          }
          if (!pick.empty() && Lower(pick) == "default.xex") {
            break;
          }
        }
      }
    }
    if (pick.empty() ||
        !ReadContainerFile(path, type, pick_path.empty() ? pick : pick_path,
                           xex)) {
      return false;
    }
  }
  return FillMetaFromXexBytes(xex, StemName(path), meta);
}

}  // namespace

bool ReadGameMeta(const std::filesystem::path& path, GameMeta& meta_out) {
  meta_out = GameMeta();
  GameFileType type = IdentifyFile(path);
  if (type == GameFileType::kUnknown) {
    return false;
  }
  meta_out.type = type;
  switch (type) {
    case GameFileType::kXex: {
      std::filesystem::path xex_path = path;
      if (std::filesystem::is_directory(path)) {
        // Extracted title: default.xex first (nested layouts included),
        // else any XEX.
        xex_path = FindExtractedXex(path);
        if (xex_path.empty()) {
          return false;
        }
      }
      std::vector<uint8_t> bytes;
      if (!ReadWholeFile(xex_path, bytes)) {
        return false;
      }
      return FillMetaFromXexBytes(bytes, StemName(path), meta_out);
    }
    case GameFileType::kStfs:
    case GameFileType::kSvod: {
      std::filesystem::path header_path;
      size_t data_file_count = 1;
      if (!ResolvePackageHeaderFile(path, header_path, data_file_count)) {
        return false;
      }
      std::vector<uint8_t> header_bytes;
      if (!ReadStfsHeader(header_path, header_bytes)) {
        return false;
      }
      const auto* header = AsContainerHeader(header_bytes);
      if (!header || !header->content_header.is_magic_valid()) {
        return false;
      }
      if (!IsGameContentType(static_cast<XContentType>(
              header->content_metadata.content_type))) {
        return false;
      }
      FillMetaFromStfsHeader(header, meta_out);
      if (meta_out.title_id.empty() || meta_out.title_id == "00000000") {
        return false;
      }
      if (meta_out.name.empty()) {
        meta_out.name = StemName(path);
      }
      meta_out.ok = true;
      return true;
    }
    case GameFileType::kIso:
    case GameFileType::kZar:
      return FillMetaFromContainerDefaultXex(path, type, meta_out);
    default:
      return false;
  }
}

std::vector<std::filesystem::path> DiscoverGameFiles(
    const std::filesystem::path& directory) {
  std::vector<std::filesystem::path> found;
  std::error_code ec = {};
  if (!std::filesystem::is_directory(directory, ec)) {
    return found;
  }
  std::vector<std::filesystem::path> queue = {directory};
  auto is_xex_name = [](const std::string& lower) {
    return lower.ends_with(".xex");
  };
  while (!queue.empty()) {
    std::filesystem::path dir = std::move(queue.back());
    queue.pop_back();
    std::vector<std::filesystem::path> xex_files;
    std::vector<std::filesystem::path> subdirs;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
      if (ec) {
        break;
      }
      std::error_code ec2 = {};
      if (e.is_directory(ec2)) {
        std::string name = Lower(e.path().filename().string());
        if (name == "$systemupdate") {
          continue;
        }
        subdirs.push_back(e.path());
        continue;
      }
      if (!e.is_regular_file(ec2)) {
        continue;
      }
      std::string lower = Lower(e.path().filename().string());
      if (lower.ends_with(".iso") || lower.ends_with(".xiso") ||
          lower.ends_with(".zar")) {
        found.push_back(e.path());
      } else if (is_xex_name(lower)) {
        xex_files.push_back(e.path());
      } else {
        // STFS/SVOD containers often have no (or odd) extensions. Signed
        // files share the magic without being games (nxeart, avatar awards,
        // title updates, live data), so require a readable header of a game
        // content type - the same check ReadGameMeta applies later.
        std::vector<uint8_t> magic;
        if (!ReadFilePrefix(e.path(), 4, magic) || magic.size() < 4) {
          continue;
        }
        fourcc_t sig = make_fourcc(magic[0], magic[1], magic[2], magic[3]);
        if (sig != vfs::kCONSignature && sig != vfs::kLIVESignature &&
            sig != vfs::kPIRSSignature) {
          continue;
        }
        std::vector<uint8_t> header;
        if (!ReadStfsHeader(e.path(), header)) {
          continue;
        }
        const auto* container = AsContainerHeader(header);
        if (container && container->content_header.is_magic_valid() &&
            IsGameContentType(static_cast<XContentType>(
                container->content_metadata.content_type))) {
          found.push_back(e.path());
        }
      }
    }
    if (!xex_files.empty()) {
      std::filesystem::path def;
      for (const auto& x : xex_files) {
        if (Lower(x.filename().string()) == "default.xex") {
          def = x;
          break;
        }
      }
      if (!def.empty()) {
        found.push_back(def);
      } else {
        found.insert(found.end(), xex_files.begin(), xex_files.end());
      }
      // Extracted titles have huge subtrees; only "content" may hold more.
      for (const auto& sub : subdirs) {
        if (Lower(sub.filename().string()) == "content") {
          queue.push_back(sub);
        }
      }
    } else {
      queue.insert(queue.end(), subdirs.begin(), subdirs.end());
    }
  }
  return found;
}

std::vector<std::filesystem::path> DiscoverInstalledGames(
    const std::filesystem::path& content_root) {
  std::vector<std::filesystem::path> found;
  std::error_code ec = {};
  const auto common_dir = content_root / "0000000000000000";
  if (!std::filesystem::is_directory(common_dir, ec)) {
    return found;
  }
  for (const auto& title_entry :
       std::filesystem::directory_iterator(common_dir, ec)) {
    if (ec) {
      break;
    }
    std::error_code ec2 = {};
    if (!title_entry.is_directory(ec2)) {
      continue;
    }
    // 000D0000 is a container directory holding installed package files
    // and/or extracted package directories.
    const auto install_dir = title_entry.path() / "000D0000";
    std::error_code ec3 = {};
    if (!std::filesystem::is_directory(install_dir, ec3)) {
      continue;
    }
    for (const auto& e :
         std::filesystem::directory_iterator(install_dir, ec3)) {
      if (ec3) {
        break;
      }
      std::error_code ec4 = {};
      if (e.is_regular_file(ec4)) {
        // Installed package file - ReadGameMeta validates the content type.
        found.push_back(e.path());
      } else if (e.is_directory(ec4)) {
        // Extracted package: launchable XEX in file form.
        auto xex = FindExtractedXex(e.path());
        if (!xex.empty()) {
          found.push_back(xex);
        }
      }
    }
  }
  return found;
}

}  // namespace wx_ui
}  // namespace app
}  // namespace xe
