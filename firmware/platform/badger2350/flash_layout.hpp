// Flash map for the 16 MiB QSPI flash on the Badger 2350 (offsets from
// XIP_BASE). The same top-of-flash arrangement as the Badger 2040, so the
// settings store and asset pack code is shared unchanged:
//
//   0x000000 - 0xFDFFFF  firmware image (checked at build time, see
//                        scripts/memory_report.py)
//   0xFE0000 - 0xFEFFFF  asset pack (64 KiB), written only by flashing
//                        badger2350_badge-assets.uf2 in BOOTSEL mode - never at runtime
//   0xFF0000 - 0xFFBFFF  unused (kept erased as a guard)
//   0xFFC000 - 0xFFFFFF  settings: slots A (0xFFC000) and B (0xFFE000), 8 KiB each,
//                        the same format as the Badger 2040's (settings_store.hpp)
//
// No partition table: images are absolute, as the boot ROM loads UF2s on a
// device without one. picotool's RP2350-E10 block (UF2 family "absolute",
// first in a flash UF2) targets 0x10FFFF00, inside settings slot B; it is
// flagged RP2_IGNORE_BLOCK and never written (scripts/verify_artifacts.py).
//
// Note: Pimoroni's BadgeWare (MicroPython) keeps a 1 MiB ROMFS above its
// 2 MiB firmware, a 12 MiB FAT filesystem and a reserved 1 MiB at the top.
// Installing this firmware replaces the firmware region; its asset and
// settings writes land in that reserved top megabyte, and the FAT data stays
// in place but is not used; see docs/INSTALL.md.
#pragma once

#include <cstdint>

namespace badge::flash_layout {

constexpr uint32_t kFlashSize = 16u * 1024 * 1024;
constexpr uint32_t kSectorSize = 4096;
constexpr uint32_t kAssetOffset = 0xFE0000;
constexpr uint32_t kAssetSize = 0x10000;
constexpr uint32_t kSettingsOffset = 0xFFC000;
constexpr uint32_t kSettingsSectors = 4;  // 2 slots x 2 sectors
constexpr uint32_t kFirmwareLimit = kAssetOffset;

static_assert(kSettingsOffset + kSettingsSectors * kSectorSize == kFlashSize, "settings at the top");
static_assert(kAssetOffset % kSectorSize == 0 && kSettingsOffset % kSectorSize == 0, "sector aligned");

}  // namespace badge::flash_layout
