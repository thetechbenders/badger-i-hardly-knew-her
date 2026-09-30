// Asset pack: a small, validated container for bitmaps (the portrait) that
// can be compiled into the firmware or flashed separately as its own UF2 to a
// reserved flash region. Built by tools/assetpack.py.
//
// Layout (little endian):
//   Header (32 bytes)
//     0  u32 magic 'BAPK'          16 u32 data_crc   (CRC32 of bytes [32, total_size))
//     4  u16 version (1)           20 u32 header_crc (CRC32 of bytes 0..19)
//     6  u16 header_size (32)      24 u8[8] reserved, must be 0
//     8  u16 entry_count
//    10  u16 flags (0)
//    12  u32 total_size
//   Entries (entry_count x 24 bytes) follow the header, then entry data.
//     0  u16 id (1 = portrait)      8 u32 offset (from pack start, 4-aligned)
//     2  u8  format (1 = MONO1)    12 u32 length
//     3  u8  reserved              16 u32 crc32 of the entry data
//     4  u16 width, 6 u16 height   20 u32 reserved
// MONO1: row-major, MSB first, 1 = black ink, stride = ceil(width / 8).
#pragma once

#include <cstddef>
#include <cstdint>

namespace badge {

constexpr uint32_t kAssetMagic = 0x4B504142;  // "BAPK"
constexpr uint16_t kAssetVersion = 1;
constexpr uint16_t kAssetIdPortrait = 1;
constexpr uint8_t kAssetFormatMono1 = 1;
constexpr uint32_t kAssetMaxEntries = 16;
constexpr uint32_t kAssetMaxSize = 64 * 1024;

enum class AssetStatus : uint8_t {
  Ok, Missing, BadMagic, BadVersion, BadHeaderCrc, BadSize, BadDataCrc, BadEntry, BadEntryCrc
};
const char *asset_status_str(AssetStatus s);

struct MonoBitmap {
  const uint8_t *bits = nullptr;
  uint16_t width = 0, height = 0, stride = 0;
  bool valid() const { return bits != nullptr && width > 0 && height > 0; }
};

struct AssetPackInfo {
  AssetStatus status = AssetStatus::Missing;
  uint16_t entries = 0;
  uint32_t total_size = 0;
  uint32_t data_crc = 0;
};

// Validate a pack located at `base` with at most `avail` readable bytes.
AssetPackInfo asset_pack_validate(const uint8_t *base, size_t avail);
// Look up a MONO1 bitmap entry in a pack that already validated OK.
bool asset_pack_bitmap(const uint8_t *base, uint16_t id, MonoBitmap *out);

}  // namespace badge
