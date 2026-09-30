// Versioned, integrity-protected settings records in two alternating flash
// sectors (A/B). A commit always writes the slot that does NOT hold the
// current record, so an interrupted erase/program leaves the previous record
// intact; the newest record with a valid CRC wins on the next boot.
//
// Record layout (little endian):
//   0  u32 magic 'BDGS'           16 u32 payload_crc (CRC32 of payload)
//   4  u16 format_version (1)     20 u32 header_crc (CRC32 of bytes 0..19)
//   6  u16 header_size (24)       24 payload: TLV [u16 id][u16 len][len bytes]...
//   8  u16 payload_len
//  10  u16 flags (0)
//  12  u32 sequence (monotonic, serial-number arithmetic)
#pragma once

#include <cstddef>
#include <cstdint>

#include "settings.hpp"

namespace badge {

class FlashBackend {
 public:
  virtual ~FlashBackend() = default;
  virtual size_t sector_size() const = 0;
  virtual int sector_count() const = 0;  // >= 2
  // Offsets are relative to the start of the settings region.
  virtual bool read(uint32_t offset, void *dst, size_t len) = 0;
  // Erase one sector and program `len` bytes (len <= sector_size) at its start.
  virtual bool erase_and_program(uint32_t sector_offset, const void *src, size_t len) = 0;
};

constexpr uint32_t kSettingsMagic = 0x53474442;  // "BDGS"
constexpr uint16_t kSettingsFormatVersion = 1;
constexpr size_t kSettingsHeaderSize = 24;
constexpr size_t kSettingsMaxRecord = 4096;

enum class DecodeStatus : uint8_t {
  Ok, Erased, BadMagic, BadHeaderCrc, UnsupportedVersion, BadLength, BadPayloadCrc, BadTlv, BadField
};
const char *decode_status_str(DecodeStatus s);

struct DecodeInfo {
  DecodeStatus status = DecodeStatus::Erased;
  uint32_t sequence = 0;
  uint16_t unknown_fields = 0;   // skipped (forward compatible)
  uint16_t rejected_fields = 0;  // failed validation -> default kept
};

// Serialise `s` into `out` (capacity cap). Returns record length or 0.
size_t settings_encode(const Settings &s, uint32_t sequence, uint8_t *out, size_t cap);
// Decode into `s`. `s` must be pre-filled with defaults: absent or rejected
// fields keep their default value. Structural errors leave `s` untouched.
DecodeInfo settings_decode(const uint8_t *rec, size_t len, Settings *s);

struct StoreStatus {
  int active_slot = -1;        // -1: running on defaults
  uint32_t sequence = 0;
  DecodeInfo slot_info[2];
  bool recovered = false;      // a slot was corrupt / torn and ignored
  uint32_t commits = 0;        // successful commits this boot
  uint32_t commit_failures = 0;
};

class SettingsStore {
 public:
  explicit SettingsStore(FlashBackend &flash) : flash_(flash) {}
  // Load the newest valid record over defaults. Always leaves `s` usable.
  void load(Settings *s);
  // Persist `s`. Returns false (and keeps the previous record) on failure.
  bool commit(const Settings &s);
  // Erase both slots (factory reset on next boot).
  bool erase_all();
  const StoreStatus &status() const { return status_; }

 private:
  FlashBackend &flash_;
  StoreStatus status_;
  uint8_t scratch_[kSettingsMaxRecord];
  Settings work_[2];
};

}  // namespace badge
