#include "assetpack.hpp"

#include "crc32.hpp"
#include "framebuffer.hpp"

namespace badge {

namespace {
uint16_t get16(const uint8_t *p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t get32(const uint8_t *p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
constexpr uint32_t kHeader = 32, kEntry = 24;
}  // namespace

const char *asset_status_str(AssetStatus s) {
  switch (s) {
    case AssetStatus::Ok: return "ok";
    case AssetStatus::Missing: return "missing";
    case AssetStatus::BadMagic: return "bad magic";
    case AssetStatus::BadVersion: return "unsupported version";
    case AssetStatus::BadHeaderCrc: return "bad header crc";
    case AssetStatus::BadSize: return "bad size";
    case AssetStatus::BadDataCrc: return "bad data crc";
    case AssetStatus::BadEntry: return "bad entry";
    case AssetStatus::BadEntryCrc: return "bad entry crc";
  }
  return "?";
}

AssetPackInfo asset_pack_validate(const uint8_t *base, size_t avail) {
  AssetPackInfo info;
  if (!base || avail < kHeader) return info;
  const uint32_t magic = get32(base);
  if (magic == 0xFFFFFFFFu || magic == 0) return info;  // erased / never flashed
  if (magic != kAssetMagic) { info.status = AssetStatus::BadMagic; return info; }
  if (get32(base + 20) != crc32(base, 20)) { info.status = AssetStatus::BadHeaderCrc; return info; }
  if (get16(base + 4) != kAssetVersion || get16(base + 6) != kHeader) {
    info.status = AssetStatus::BadVersion;
    return info;
  }
  // Reserved header bytes are outside both CRCs, so require them to be zero.
  for (uint32_t i = 24; i < kHeader; ++i)
    if (base[i]) { info.status = AssetStatus::BadVersion; return info; }
  const uint16_t count = get16(base + 8);
  const uint32_t total = get32(base + 12);
  if (count > kAssetMaxEntries || total > kAssetMaxSize || total > avail ||
      total < kHeader + uint32_t(count) * kEntry) {
    info.status = AssetStatus::BadSize;
    return info;
  }
  if (get32(base + 16) != crc32(base + kHeader, total - kHeader)) {
    info.status = AssetStatus::BadDataCrc;
    return info;
  }
  const uint32_t data_start = kHeader + uint32_t(count) * kEntry;
  for (uint16_t i = 0; i < count; ++i) {
    const uint8_t *e = base + kHeader + uint32_t(i) * kEntry;
    const uint8_t fmt = e[2];
    const uint16_t w = get16(e + 4), h = get16(e + 6);
    const uint32_t off = get32(e + 8), len = get32(e + 12);
    if (off < data_start || (off & 3) || len > total || off > total - len) {
      info.status = AssetStatus::BadEntry;
      return info;
    }
    if (fmt == kAssetFormatMono1) {
      if (w == 0 || h == 0 || w > Framebuffer::kWidth || h > Framebuffer::kHeight ||
          len != uint32_t((w + 7) / 8) * h) {
        info.status = AssetStatus::BadEntry;
        return info;
      }
    }
    if (crc32(base + off, len) != get32(e + 16)) { info.status = AssetStatus::BadEntryCrc; return info; }
  }
  info.status = AssetStatus::Ok;
  info.entries = count;
  info.total_size = total;
  info.data_crc = get32(base + 16);
  return info;
}

bool asset_pack_bitmap(const uint8_t *base, uint16_t id, MonoBitmap *out) {
  const uint16_t count = get16(base + 8);
  for (uint16_t i = 0; i < count; ++i) {
    const uint8_t *e = base + kHeader + uint32_t(i) * kEntry;
    if (get16(e) == id && e[2] == kAssetFormatMono1) {
      out->width = get16(e + 4);
      out->height = get16(e + 6);
      out->stride = uint16_t((out->width + 7) / 8);
      out->bits = base + get32(e + 8);
      return true;
    }
  }
  return false;
}

}  // namespace badge
