// Flash map for the 2 MiB W25Q16 on the Badger 2040 (offsets from XIP_BASE).
//
//   0x000000 - 0x1DFFFF  firmware image (checked at build time, see
//                        scripts/memory_report.py; currently ~150 KiB)
//   0x1E0000 - 0x1EFFFF  asset pack (64 KiB), written only by flashing
//                        assets.uf2 in BOOTSEL mode - never at runtime
//   0x1F0000 - 0x1FDFFF  unused (kept erased as a guard)
//   0x1FE000 - 0x1FFFFF  settings slots A and B (2 x 4 KiB sectors)
//
// Note: stock MicroPython/BadgerOS keeps its LittleFS filesystem in the top
// 1 MiB (2022 releases) or 1408 KiB (later releases) of flash. Installing this firmware leaves most of that data in
// place until our settings/asset writes overwrite the top sectors, after
// which BadgerOS's filesystem is corrupt; see docs/INSTALL.md.
#pragma once

#include <cstdint>

namespace badge::flash_layout {

constexpr uint32_t kFlashSize = 2u * 1024 * 1024;
constexpr uint32_t kSectorSize = 4096;
constexpr uint32_t kAssetOffset = 0x1E0000;
constexpr uint32_t kAssetSize = 0x10000;
constexpr uint32_t kSettingsOffset = 0x1FE000;
constexpr uint32_t kSettingsSectors = 2;
constexpr uint32_t kFirmwareLimit = kAssetOffset;

static_assert(kSettingsOffset + kSettingsSectors * kSectorSize == kFlashSize, "settings at the top");
static_assert(kAssetOffset % kSectorSize == 0 && kSettingsOffset % kSectorSize == 0, "sector aligned");

}  // namespace badge::flash_layout
