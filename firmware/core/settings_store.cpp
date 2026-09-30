#include "settings_store.hpp"

#include <cstring>

#include "crc32.hpp"
#include "text.hpp"

namespace badge {

namespace {

void put16(uint8_t *p, uint16_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }
void put32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i)); }
uint16_t get16(const uint8_t *p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t get32(const uint8_t *p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

// Serial-number comparison so the sequence can wrap.
bool seq_newer(uint32_t a, uint32_t b) { return int32_t(a - b) > 0; }

}  // namespace

const char *decode_status_str(DecodeStatus s) {
  switch (s) {
    case DecodeStatus::Ok: return "ok";
    case DecodeStatus::Erased: return "erased";
    case DecodeStatus::BadMagic: return "bad magic";
    case DecodeStatus::BadHeaderCrc: return "bad header crc";
    case DecodeStatus::UnsupportedVersion: return "unsupported version";
    case DecodeStatus::BadLength: return "bad length";
    case DecodeStatus::BadPayloadCrc: return "bad payload crc";
    case DecodeStatus::BadTlv: return "bad tlv";
    case DecodeStatus::BadField: return "bad field";
  }
  return "?";
}

size_t settings_encode(const Settings &s, uint32_t sequence, uint8_t *out, size_t cap) {
  if (cap < kSettingsHeaderSize) return 0;
  size_t n;
  const FieldDesc *fields = settings_fields(&n);
  size_t pos = kSettingsHeaderSize;
  const uint8_t *base = reinterpret_cast<const uint8_t *>(&s);
  for (size_t i = 0; i < n; ++i) {
    const FieldDesc &f = fields[i];
    size_t len = f.size;
    if (f.type == FieldType::Str) {
      len = cstr_len(reinterpret_cast<const char *>(base + f.offset), f.size);
      if (len >= f.size) return 0;  // not NUL terminated: refuse to persist
    }
    if (pos + 4 + len > cap) return 0;
    put16(out + pos, f.id);
    put16(out + pos + 2, uint16_t(len));
    std::memcpy(out + pos + 4, base + f.offset, len);
    pos += 4 + len;
  }
  const size_t payload_len = pos - kSettingsHeaderSize;
  put32(out + 0, kSettingsMagic);
  put16(out + 4, kSettingsFormatVersion);
  put16(out + 6, uint16_t(kSettingsHeaderSize));
  put16(out + 8, uint16_t(payload_len));
  put16(out + 10, 0);
  put32(out + 12, sequence);
  put32(out + 16, crc32(out + kSettingsHeaderSize, payload_len));
  put32(out + 20, crc32(out, 20));
  return pos;
}

DecodeInfo settings_decode(const uint8_t *rec, size_t len, Settings *s) {
  DecodeInfo info;
  if (len < kSettingsHeaderSize) { info.status = DecodeStatus::BadLength; return info; }
  bool erased = true;
  for (size_t i = 0; i < kSettingsHeaderSize; ++i) erased &= rec[i] == 0xFF;
  if (erased) { info.status = DecodeStatus::Erased; return info; }
  if (get32(rec) != kSettingsMagic) { info.status = DecodeStatus::BadMagic; return info; }
  if (get32(rec + 20) != crc32(rec, 20)) { info.status = DecodeStatus::BadHeaderCrc; return info; }
  if (get16(rec + 4) != kSettingsFormatVersion) { info.status = DecodeStatus::UnsupportedVersion; return info; }
  const uint16_t hsize = get16(rec + 6);
  const uint16_t plen = get16(rec + 8);
  if (hsize != kSettingsHeaderSize || size_t(hsize) + plen > len || size_t(hsize) + plen > kSettingsMaxRecord) {
    info.status = DecodeStatus::BadLength;
    return info;
  }
  const uint8_t *p = rec + hsize;
  if (get32(rec + 16) != crc32(p, plen)) { info.status = DecodeStatus::BadPayloadCrc; return info; }

  // First pass: structural TLV validation, so a malformed record never
  // partially applies.
  for (size_t pos = 0; pos < plen;) {
    if (pos + 4 > plen) { info.status = DecodeStatus::BadTlv; return info; }
    const uint16_t flen = get16(p + pos + 2);
    if (pos + 4 + flen > plen) { info.status = DecodeStatus::BadTlv; return info; }
    pos += 4 + flen;
  }
  // Second pass: apply known, valid fields. Nothing below can abort, so the
  // record is applied whole (minus individually rejected fields).
  uint8_t *base = reinterpret_cast<uint8_t *>(s);
  for (size_t pos = 0; pos < plen;) {
    const uint16_t id = get16(p + pos);
    const uint16_t flen = get16(p + pos + 2);
    const uint8_t *val = p + pos + 4;
    pos += 4 + flen;
    const FieldDesc *f = find_field_id(id);
    if (!f) { ++info.unknown_fields; continue; }
    if (f->type == FieldType::Str) {
      if (flen >= f->size || !utf8_valid_printable(reinterpret_cast<const char *>(val), flen) ||
          std::memchr(val, 0, flen)) {
        ++info.rejected_fields;
        continue;
      }
      std::memset(base + f->offset, 0, f->size);
      std::memcpy(base + f->offset, val, flen);
    } else {
      if (flen != f->size) { ++info.rejected_fields; continue; }
      uint32_t v = f->size == 1 ? val[0] : get16(val);
      if (v < f->min || v > f->max || (f->id == 0x304 && v != 0 && v < 15)) {
        ++info.rejected_fields;
        continue;
      }
      std::memcpy(base + f->offset, val, f->size);
    }
  }
  info.sequence = get32(rec + 12);
  info.status = DecodeStatus::Ok;
  return info;
}

void SettingsStore::load(Settings *s) {
  status_ = StoreStatus{};
  // Candidates live in the store object: Settings is ~2.6 KiB and the RP2040
  // stacks are small.
  Settings *candidate = work_;
  bool ok[2] = {false, false};
  const size_t n = flash_.sector_size() < kSettingsMaxRecord ? flash_.sector_size() : kSettingsMaxRecord;
  for (int slot = 0; slot < 2; ++slot) {
    std::memcpy(&candidate[slot], s, sizeof *s);
    if (!flash_.read(uint32_t(slot) * uint32_t(flash_.sector_size()), scratch_, n)) {
      status_.slot_info[slot].status = DecodeStatus::BadLength;
      status_.recovered = true;
      continue;
    }
    status_.slot_info[slot] = settings_decode(scratch_, n, &candidate[slot]);
    ok[slot] = status_.slot_info[slot].status == DecodeStatus::Ok;
    if (!ok[slot] && status_.slot_info[slot].status != DecodeStatus::Erased) status_.recovered = true;
  }
  int pick = -1;
  if (ok[0] && ok[1]) pick = seq_newer(status_.slot_info[1].sequence, status_.slot_info[0].sequence) ? 1 : 0;
  else if (ok[0]) pick = 0;
  else if (ok[1]) pick = 1;
  if (pick >= 0) {
    std::memcpy(s, &candidate[pick], sizeof *s);
    status_.active_slot = pick;
    status_.sequence = status_.slot_info[pick].sequence;
  }
}

bool SettingsStore::commit(const Settings &s) {
  const uint32_t seq = status_.active_slot >= 0 ? status_.sequence + 1 : 1;
  const int target = status_.active_slot == 0 ? 1 : 0;
  const size_t len = settings_encode(s, seq, scratch_, flash_.sector_size() < kSettingsMaxRecord
                                                           ? flash_.sector_size() : kSettingsMaxRecord);
  if (!len) { ++status_.commit_failures; return false; }
  const uint32_t off = uint32_t(target) * uint32_t(flash_.sector_size());
  if (!flash_.erase_and_program(off, scratch_, len)) { ++status_.commit_failures; return false; }
  // Read back and fully decode before trusting the new slot.
  if (!flash_.read(off, scratch_, len)) { ++status_.commit_failures; return false; }
  Settings &verify = work_[0];
  std::memcpy(&verify, &s, sizeof verify);
  const DecodeInfo di = settings_decode(scratch_, len, &verify);
  if (di.status != DecodeStatus::Ok || di.sequence != seq || di.rejected_fields ||
      std::memcmp(&verify, &s, sizeof verify) != 0) {
    ++status_.commit_failures;
    return false;
  }
  status_.active_slot = target;
  status_.sequence = seq;
  status_.slot_info[target] = di;
  ++status_.commits;
  return true;
}

bool SettingsStore::erase_all() {
  std::memset(scratch_, 0xFF, 16);
  bool ok = true;
  for (int slot = 0; slot < 2; ++slot)
    ok &= flash_.erase_and_program(uint32_t(slot) * uint32_t(flash_.sector_size()), scratch_, 0);
  status_.active_slot = -1;
  status_.sequence = 0;
  return ok;
}

}  // namespace badge
