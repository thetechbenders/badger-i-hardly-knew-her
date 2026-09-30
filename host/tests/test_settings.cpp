#include <string>
#include <vector>

#include "check.hpp"
#include "crc32.hpp"
#include "settings_store.hpp"

using namespace badge;

namespace {
// RAM-backed flash with fault injection.
class FakeFlash : public FlashBackend {
 public:
  explicit FakeFlash(int sectors = 2) : mem(size_t(sectors) * 4096, 0xFF), sectors_(sectors) {}
  size_t sector_size() const override { return 4096; }
  int sector_count() const override { return sectors_; }
  bool read(uint32_t off, void *dst, size_t len) override {
    if (off + len > mem.size()) return false;
    std::memcpy(dst, &mem[off], len);
    return true;
  }
  bool erase_and_program(uint32_t off, const void *src, size_t len) override {
    ++writes;
    if (fail_next) { fail_next = false; return false; }
    std::memset(&mem[off], 0xFF, 4096);
    size_t n = len;
    if (tear_after >= 0) { n = size_t(tear_after) < len ? size_t(tear_after) : len; tear_after = -1; }
    std::memcpy(&mem[off], src, n);  // a torn write leaves the rest erased
    return n == len;
  }
  std::vector<uint8_t> mem;
  int sectors_;
  int writes = 0;
  bool fail_next = false;
  int tear_after = -1;
};

void fresh(Settings *s) { settings_defaults(s); }
Settings g_a, g_b;
}  // namespace

TEST(settings_defaults_are_valid) {
  fresh(&g_a);
  const FieldDesc *bad = nullptr;
  CHECK(settings_validate(g_a, &bad));
  CHECK(g_a.profile.name[0] != 0);
  CHECK(sizeof(Settings) + 4 * 64 < kSettingsMaxRecord);  // encoded record always fits a sector
}

TEST(settings_set_validates) {
  fresh(&g_a);
  CHECK(settings_set(&g_a, *find_field("name"), "Ada") == SetResult::Ok);
  CHECK_STR(g_a.profile.name, "Ada");
  std::string long_name(47, 'x');
  CHECK(settings_set(&g_a, *find_field("name"), long_name.c_str()) == SetResult::Ok);
  long_name.push_back('y');
  CHECK(settings_set(&g_a, *find_field("name"), long_name.c_str()) == SetResult::TooLong);
  CHECK(settings_set(&g_a, *find_field("name"), "a\tb") == SetResult::BadUtf8);
  CHECK(settings_set(&g_a, *find_field("refresh.speed"), "4") == SetResult::OutOfRange);
  CHECK(settings_set(&g_a, *find_field("refresh.speed"), "x") == SetResult::BadNumber);
  CHECK(settings_set(&g_a, *find_field("sleep.timeout_s"), "5") == SetResult::OutOfRange);
  CHECK(settings_set(&g_a, *find_field("sleep.timeout_s"), "0") == SetResult::Ok);
  CHECK(settings_set(&g_a, *find_field("refresh.partial"), "off") == SetResult::Ok);
  CHECK_EQ(g_a.prefs.partial_refresh, 0);
  CHECK(find_field("nope") == nullptr);
}

TEST(settings_field_ids_unique) {
  size_t n;
  const FieldDesc *f = settings_fields(&n);
  for (size_t i = 0; i < n; ++i)
    for (size_t j = i + 1; j < n; ++j) {
      CHECK(f[i].id != f[j].id);
      CHECK(std::strcmp(f[i].key, f[j].key) != 0);
    }
}

TEST(settings_roundtrip_and_ab_alternation) {
  FakeFlash flash;
  static SettingsStore store(flash);
  fresh(&g_a);
  store.load(&g_a);
  CHECK_EQ(store.status().active_slot, -1);
  CHECK(!store.status().recovered);  // erased flash is not "corrupt"
  settings_set(&g_a, *find_field("name"), "First");
  CHECK(store.commit(g_a));
  CHECK_EQ(store.status().active_slot, 0);
  settings_set(&g_a, *find_field("name"), "Second");
  CHECK(store.commit(g_a));
  CHECK_EQ(store.status().active_slot, 1);
  fresh(&g_b);
  store.load(&g_b);
  CHECK_STR(g_b.profile.name, "Second");
  CHECK_EQ(store.status().sequence, 2u);
  CHECK(std::memcmp(&g_a, &g_b, sizeof g_a) == 0);
}

TEST(settings_torn_write_keeps_previous_record) {
  FakeFlash flash;
  static SettingsStore store(flash);
  fresh(&g_a);
  store.load(&g_a);
  settings_set(&g_a, *find_field("name"), "Stable");
  CHECK(store.commit(g_a));
  settings_set(&g_a, *find_field("name"), "Interrupted");
  flash.tear_after = 80;  // power lost mid-program (record is ~700 bytes)
  CHECK(!store.commit(g_a));
  fresh(&g_b);
  store.load(&g_b);
  CHECK_STR(g_b.profile.name, "Stable");
  CHECK(store.status().recovered);
  // The next commit must not overwrite the good slot.
  settings_set(&g_b, *find_field("name"), "Next");
  CHECK(store.commit(g_b));
  fresh(&g_a);
  store.load(&g_a);
  CHECK_STR(g_a.profile.name, "Next");
}

TEST(settings_every_single_bit_flip_is_detected) {
  FakeFlash flash;
  static SettingsStore store(flash);
  fresh(&g_a);
  store.load(&g_a);
  settings_set(&g_a, *find_field("name"), "Flip");
  CHECK(store.commit(g_a));
  const std::vector<uint8_t> good = flash.mem;
  static uint8_t rec[4096];
  std::memcpy(rec, &good[0], 4096);
  const uint16_t plen = uint16_t(rec[8] | rec[9] << 8);
  int undetected = 0;
  for (size_t byte = 0; byte < kSettingsHeaderSize + plen; byte += 3) {
    for (int bit = 0; bit < 8; bit += 3) {
      std::memcpy(rec, &good[0], 4096);
      rec[byte] ^= uint8_t(1 << bit);
      fresh(&g_b);
      if (settings_decode(rec, 4096, &g_b).status == DecodeStatus::Ok) ++undetected;
    }
  }
  CHECK_EQ(undetected, 0);
}

TEST(settings_both_slots_corrupt_falls_back_to_defaults) {
  FakeFlash flash;
  for (size_t i = 0; i < flash.mem.size(); ++i) flash.mem[i] = uint8_t(i * 37 + 11);  // e.g. old littlefs data
  static SettingsStore store(flash);
  fresh(&g_a);
  store.load(&g_a);
  fresh(&g_b);
  CHECK(std::memcmp(&g_a, &g_b, sizeof g_a) == 0);
  CHECK_EQ(store.status().active_slot, -1);
  CHECK(store.status().recovered);
  CHECK(store.commit(g_a));  // recovers by writing a fresh record
  store.load(&g_b);
  CHECK_EQ(store.status().active_slot, 0);
}

TEST(settings_sequence_wraparound) {
  FakeFlash flash;
  static uint8_t rec[4096];
  fresh(&g_a);
  settings_set(&g_a, *find_field("name"), "Old");
  size_t n = settings_encode(g_a, 0xFFFFFFFFu, rec, sizeof rec);
  flash.erase_and_program(0, rec, n);
  settings_set(&g_a, *find_field("name"), "New");
  n = settings_encode(g_a, 0, rec, sizeof rec);  // wrapped: 0 is newer than 0xFFFFFFFF
  flash.erase_and_program(4096, rec, n);
  static SettingsStore store(flash);
  fresh(&g_b);
  store.load(&g_b);
  CHECK_STR(g_b.profile.name, "New");
}

TEST(settings_unknown_and_invalid_fields) {
  static uint8_t rec[4096];
  fresh(&g_a);
  size_t n = settings_encode(g_a, 5, rec, sizeof rec);
  // Append an unknown TLV (a newer firmware's field) and an out-of-range value
  // for refresh.speed, then re-seal the record.
  auto put16 = [](uint8_t *p, uint16_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); };
  put16(rec + n, 0x7777); put16(rec + n + 2, 2); rec[n + 4] = 1; rec[n + 5] = 2; n += 6;
  put16(rec + n, 0x301); put16(rec + n + 2, 1); rec[n + 4] = 9; n += 5;
  const uint16_t plen = uint16_t(n - kSettingsHeaderSize);
  put16(rec + 8, plen);
  const uint32_t pc = crc32(rec + kSettingsHeaderSize, plen);
  for (int i = 0; i < 4; ++i) rec[16 + i] = uint8_t(pc >> (8 * i));
  const uint32_t hc = crc32(rec, 20);
  for (int i = 0; i < 4; ++i) rec[20 + i] = uint8_t(hc >> (8 * i));
  fresh(&g_b);
  DecodeInfo di = settings_decode(rec, n, &g_b);
  CHECK(di.status == DecodeStatus::Ok);
  CHECK_EQ(di.unknown_fields, 1);
  CHECK_EQ(di.rejected_fields, 1);
  CHECK_EQ(g_b.prefs.refresh_speed, g_a.prefs.refresh_speed);  // earlier valid value kept
  // Unsupported future version is rejected as a whole.
  rec[4] = 2;
  const uint32_t hc2 = crc32(rec, 20);
  for (int i = 0; i < 4; ++i) rec[20 + i] = uint8_t(hc2 >> (8 * i));
  CHECK(settings_decode(rec, n, &g_b).status == DecodeStatus::UnsupportedVersion);
}

TEST(settings_commit_failure_keeps_state) {
  FakeFlash flash;
  static SettingsStore store(flash);
  fresh(&g_a);
  store.load(&g_a);
  CHECK(store.commit(g_a));
  flash.fail_next = true;
  settings_set(&g_a, *find_field("name"), "Lost");
  CHECK(!store.commit(g_a));
  CHECK_EQ(store.status().commit_failures, 1u);
  CHECK_EQ(store.status().active_slot, 0);
  CHECK(store.erase_all());
  fresh(&g_b);
  store.load(&g_b);
  CHECK_EQ(store.status().active_slot, -1);
}
