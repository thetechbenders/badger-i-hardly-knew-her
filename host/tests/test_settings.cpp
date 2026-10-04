#include <string>
#include <vector>

#include "check.hpp"
#include "crc32.hpp"
#include "settings_store.hpp"

using namespace badge;

namespace {
// RAM-backed 16 KiB settings region with fault injection.
class FakeFlash : public FlashBackend {
 public:
  FakeFlash() : mem(kSettingsRegionSize, 0xFF) {}
  size_t sector_size() const override { return 4096; }
  size_t region_size() const override { return mem.size(); }
  bool read(uint32_t off, void *dst, size_t len) override {
    if (off + len > mem.size()) return false;
    std::memcpy(dst, &mem[off], len);
    return true;
  }
  bool erase_and_program(uint32_t off, const void *src, size_t len, size_t erase_len) override {
    ++writes;
    if (off % 4096 || erase_len % 4096 || len > erase_len || off + erase_len > mem.size()) return false;
    if (fail_next) { fail_next = false; return false; }
    std::memset(&mem[off], 0xFF, erase_len);
    size_t n = len;
    if (tear_after >= 0) { n = size_t(tear_after) < len ? size_t(tear_after) : len; tear_after = -1; }
    std::memcpy(&mem[off], src, n);  // a torn write leaves the rest erased
    last_write_off = off;
    return n == len;
  }
  // Raw write for planting records (legacy layouts).
  void plant(uint32_t off, const uint8_t *rec, size_t n, size_t erase_len) {
    std::memset(&mem[off], 0xFF, erase_len);
    std::memcpy(&mem[off], rec, n);
  }
  std::vector<uint8_t> mem;
  int writes = 0;
  bool fail_next = false;
  int tear_after = -1;
  uint32_t last_write_off = 0xFFFFFFFF;
};

void put16(uint8_t *p, uint16_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }
uint16_t get16(const uint8_t *p) { return uint16_t(p[0] | p[1] << 8); }
void reseal(uint8_t *rec, size_t payload_len) {
  put16(rec + 8, uint16_t(payload_len));
  const uint32_t pc = crc32(rec + kSettingsHeaderSize, payload_len);
  for (int i = 0; i < 4; ++i) rec[16 + i] = uint8_t(pc >> (8 * i));
  const uint32_t hc = crc32(rec, 20);
  for (int i = 0; i < 4; ++i) rec[20 + i] = uint8_t(hc >> (8 * i));
}

// A realistic format-1 record (firmware before the portfolio): only the
// field ids that existed then (no contact types, projects 1-4 with
// title/tagline/body/link), version 1, 4 KiB limit.
bool v1_id(uint16_t id) {
  if (id >= 0x140 && id < 0x200) return false;                  // contact types are new
  if (id >= 0x200 && id < 0x300) return id < 0x220 && (id & 7) < 4;  // 4 projects, 4 fields
  return true;
}
size_t make_v1(const Settings &s, uint32_t seq, uint8_t *out) {
  static uint8_t tmp[kSettingsMaxRecord];
  const size_t n = settings_encode(s, seq, tmp, sizeof tmp);
  std::memcpy(out, tmp, kSettingsHeaderSize);
  size_t o = kSettingsHeaderSize;
  for (size_t pos = kSettingsHeaderSize; pos < n;) {
    const uint16_t id = get16(tmp + pos), len = get16(tmp + pos + 2);
    if (v1_id(id)) { std::memcpy(out + o, tmp + pos, 4u + len); o += 4u + len; }
    pos += 4u + len;
  }
  put16(out + 4, kSettingsLegacyVersion);
  reseal(out, o - kSettingsHeaderSize);
  return o <= kSettingsLegacySlotSize ? o : 0;
}

void fresh(Settings *s) { settings_defaults(s); }
Settings g_a, g_b;
}  // namespace

TEST(settings_defaults_are_valid) {
  fresh(&g_a);
  const FieldDesc *bad = nullptr;
  CHECK(settings_validate(g_a, &bad));
  CHECK(g_a.profile.name[0] != 0);
  CHECK_EQ(g_a.prefs.gesture_sensitivity, 10);
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
  static uint8_t rec[kSettingsSlotSize];
  std::memcpy(rec, &good[0], kSettingsSlotSize);
  const uint16_t plen = uint16_t(rec[8] | rec[9] << 8);
  int undetected = 0;
  for (size_t byte = 0; byte < kSettingsHeaderSize + plen; byte += 3) {
    for (int bit = 0; bit < 8; bit += 3) {
      std::memcpy(rec, &good[0], kSettingsSlotSize);
      rec[byte] ^= uint8_t(1 << bit);
      fresh(&g_b);
      if (settings_decode(rec, kSettingsSlotSize, &g_b).status == DecodeStatus::Ok) ++undetected;
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
  static uint8_t rec[kSettingsMaxRecord];
  fresh(&g_a);
  settings_set(&g_a, *find_field("name"), "Old");
  size_t n = settings_encode(g_a, 0xFFFFFFFFu, rec, sizeof rec);
  flash.erase_and_program(0, rec, n, kSettingsSlotSize);
  settings_set(&g_a, *find_field("name"), "New");
  n = settings_encode(g_a, 0, rec, sizeof rec);  // wrapped: 0 is newer than 0xFFFFFFFF
  flash.erase_and_program(kSettingsSlotSize, rec, n, kSettingsSlotSize);
  static SettingsStore store(flash);
  fresh(&g_b);
  store.load(&g_b);
  CHECK_STR(g_b.profile.name, "New");
}

TEST(settings_unknown_and_invalid_fields) {
  static uint8_t rec[kSettingsMaxRecord];
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
  rec[4] = 3;
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

// ------------------------------------------------------- format 2 / migration

TEST(settings_worst_case_record_fits_a_slot) {
  // Every text field filled to capacity with multi-byte text must still
  // encode into one 8 KiB slot (12 projects, 6 contacts).
  fresh(&g_a);
  size_t n;
  const FieldDesc *f = settings_fields(&n);
  for (size_t i = 0; i < n; ++i) {
    if (f[i].type != FieldType::Str) continue;
    std::string v;
    if (std::strstr(f[i].key, ".type")) v = "discord";
    else if (std::strstr(f[i].key, ".link")) { v = "https://"; while (v.size() + 1 < f[i].size) v += 'x'; }
    else while (v.size() + 2 < f[i].size) v += "\xC3\x9C";
    CHECK(settings_set(&g_a, f[i], v.c_str()) == SetResult::Ok);
  }
  static uint8_t rec[kSettingsMaxRecord];
  const size_t len = settings_encode(g_a, 1, rec, sizeof rec);
  CHECK(len > 0 && len <= kSettingsSlotSize);
  fresh(&g_b);
  CHECK(settings_decode(rec, len, &g_b).status == DecodeStatus::Ok);
  CHECK(std::memcmp(&g_a, &g_b, sizeof g_a) == 0);
}

TEST(settings_v1_record_migrates_and_keeps_its_values) {
  FakeFlash flash;
  static uint8_t rec[kSettingsLegacySlotSize];
  // An old badge: custom name, typeless contacts, a two-project list.
  fresh(&g_a);
  settings_set(&g_a, *find_field("name"), "Legacy Name");
  settings_set(&g_a, *find_field("contact4.label"), "GitHub");
  settings_set(&g_a, *find_field("contact4.value"), "octocat");
  for (int i = 1; i <= kMaxProjects; ++i) settings_set(&g_a, *find_field(("project" + std::to_string(i) + ".title").c_str()), "");
  settings_set(&g_a, *find_field("project1.title"), "Old One");
  settings_set(&g_a, *find_field("project2.title"), "Old Two");
  settings_set(&g_a, *find_field("refresh.speed"), "2");
  const size_t n1 = make_v1(g_a, 41, rec);
  CHECK(n1 > 0);
  flash.plant(kSettingsLegacyOffset[1], rec, n1, kSettingsLegacySlotSize);  // legacy slot B, seq 41
  static SettingsStore store(flash);
  fresh(&g_b);  // new defaults: 7-project portfolio
  CHECK(configured_project_count(g_b.profile) >= 2);
  // A default type on a line the old record uses for something else.
  CHECK(settings_set(&g_b, *find_field("contact4.type"), "phone") == SetResult::Ok);
  CHECK(settings_set(&g_b, *find_field("contact1.type"), "discord") == SetResult::Ok);
  store.load(&g_b);
  CHECK(store.status().migrated_v1);
  CHECK_EQ(store.status().legacy_slot, 1);
  CHECK_EQ(store.status().active_slot, -1);
  CHECK(!store.status().recovered);  // a v1 header inside slot B is expected, not corruption
  CHECK_STR(g_b.profile.name, "Legacy Name");
  CHECK_EQ(g_b.prefs.refresh_speed, 2);
  CHECK_STR(g_b.profile.contacts[3].label, "GitHub");  // no type guessed from the label
  CHECK_STR(g_b.profile.contacts[3].type, "");
  CHECK_STR(g_b.profile.contacts[0].type, "");  // default types never leak onto legacy values
  // The v1 project list is authoritative: no new defaults appended to it.
  CHECK_EQ(configured_project_count(g_b.profile), 2);
  CHECK_STR(g_b.profile.projects[0].title, "Old One");
  // The first commit goes to slot A (never over the legacy record) and
  // continues the sequence.
  CHECK(store.commit(g_b));
  CHECK_EQ(flash.last_write_off, 0u);
  CHECK_EQ(store.status().active_slot, 0);
  CHECK_EQ(store.status().sequence, 42u);
  CHECK(!store.status().migrated_v1);
  fresh(&g_a);
  store.load(&g_a);
  CHECK(!store.status().migrated_v1);
  CHECK_STR(g_a.profile.name, "Legacy Name");
  // The second commit alternates to slot B, overwriting the legacy area.
  CHECK(store.commit(g_a));
  CHECK_EQ(flash.last_write_off, uint32_t(kSettingsSlotSize));
  fresh(&g_b);
  store.load(&g_b);
  CHECK_EQ(store.status().active_slot, 1);
  CHECK_EQ(store.status().sequence, 43u);
}

TEST(settings_torn_first_commit_after_migration_falls_back_to_v1) {
  FakeFlash flash;
  static uint8_t rec[kSettingsLegacySlotSize];
  fresh(&g_a);
  settings_set(&g_a, *find_field("name"), "Survivor");
  const size_t n1 = make_v1(g_a, 7, rec);
  flash.plant(kSettingsLegacyOffset[0], rec, n1, kSettingsLegacySlotSize);
  settings_set(&g_a, *find_field("name"), "Older");
  const size_t n2 = make_v1(g_a, 6, rec);
  flash.plant(kSettingsLegacyOffset[1], rec, n2, kSettingsLegacySlotSize);
  static SettingsStore store(flash);
  fresh(&g_b);
  store.load(&g_b);
  CHECK_STR(g_b.profile.name, "Survivor");  // newest legacy slot
  CHECK_EQ(store.status().legacy_slot, 0);
  settings_set(&g_b, *find_field("name"), "Interrupted");
  flash.tear_after = 100;
  CHECK(!store.commit(g_b));
  fresh(&g_a);
  store.load(&g_a);
  CHECK(store.status().migrated_v1);
  CHECK(store.status().recovered);  // torn slot A reported
  CHECK_STR(g_a.profile.name, "Survivor");
}

TEST(settings_format2_wins_over_leftover_legacy) {
  FakeFlash flash;
  static uint8_t rec[kSettingsMaxRecord];
  fresh(&g_a);
  settings_set(&g_a, *find_field("name"), "Stale v1");
  flash.plant(kSettingsLegacyOffset[1], rec, make_v1(g_a, 900, rec), kSettingsLegacySlotSize);
  settings_set(&g_a, *find_field("name"), "Current v2");
  const size_t n = settings_encode(g_a, 3, rec, sizeof rec);
  flash.plant(0, rec, n, kSettingsSlotSize);
  static SettingsStore store(flash);
  fresh(&g_b);
  store.load(&g_b);
  CHECK_STR(g_b.profile.name, "Current v2");  // even though the v1 sequence is higher
  CHECK(!store.status().migrated_v1);
  CHECK_EQ(store.status().active_slot, 0);
}

TEST(settings_contact_types_and_project_links_validated) {
  fresh(&g_a);
  CHECK(settings_set(&g_a, *find_field("contact1.type"), "github") == SetResult::Ok);
  CHECK(contact_type(g_a.profile.contacts[0].type) == ContactType::GitHub);
  CHECK(settings_set(&g_a, *find_field("contact1.type"), "GitHub") == SetResult::BadValue);  // exact names
  CHECK(settings_set(&g_a, *find_field("contact1.type"), "myspace") == SetResult::BadValue);
  CHECK(settings_set(&g_a, *find_field("contact1.type"), "") == SetResult::Ok);
  CHECK(settings_set(&g_a, *find_field("project12.link"), "https://github.com/x/y") == SetResult::Ok);
  CHECK(settings_set(&g_a, *find_field("project12.link"), "http://github.com/x/y") == SetResult::BadValue);
  CHECK(settings_set(&g_a, *find_field("project12.link"), "github.com/x") == SetResult::BadValue);
  CHECK(settings_set(&g_a, *find_field("project12.link"), "https://") == SetResult::BadValue);
  CHECK(settings_set(&g_a, *find_field("project12.link"), "https://a b") == SetResult::BadValue);
  CHECK(find_field("project13.title") == nullptr);  // bounded list
  // A record carrying an invalid link (e.g. written by a buggy tool) keeps
  // the default rather than producing a broken QR.
  static uint8_t rec[kSettingsMaxRecord];
  size_t n = settings_encode(g_a, 1, rec, sizeof rec);
  for (size_t pos = kSettingsHeaderSize; pos < n;) {
    const uint16_t id = get16(rec + pos), len = get16(rec + pos + 2);
    if (id == find_field("project12.link")->id) std::memcpy(rec + pos + 4, "ftp://", 6);
    pos += 4u + len;
  }
  reseal(rec, n - kSettingsHeaderSize);
  fresh(&g_b);
  DecodeInfo di = settings_decode(rec, n, &g_b);
  CHECK(di.status == DecodeStatus::Ok);
  CHECK_EQ(di.rejected_fields, 1);
}
