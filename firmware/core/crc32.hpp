// CRC-32 (IEEE 802.3, reflected, poly 0xEDB88320) - matches Python zlib.crc32.
#pragma once

#include <cstddef>
#include <cstdint>

namespace badge {

uint32_t crc32_update(uint32_t crc, const void *data, size_t len);
inline uint32_t crc32(const void *data, size_t len) { return crc32_update(0, data, len); }

}  // namespace badge
