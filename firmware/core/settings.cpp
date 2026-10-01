#include "settings.hpp"

#include <cstdio>
#include <cstring>

#include "text.hpp"

namespace badge {

// Generated from the selected profile JSON by tools/profilegen.py.
struct DefaultEntry {
  const char *key;
  const char *value;
};
extern const DefaultEntry kDefaultProfile[];
extern const size_t kDefaultProfileCount;

namespace {

#define STR_FIELD(key, id, member, help) \
  {key, id, FieldType::Str, uint16_t(offsetof(Settings, member)), uint16_t(sizeof(Settings::member)), 0, 0, help}
#define NUM_FIELD(key, id, type, member, lo, hi, help) \
  {key, id, type, uint16_t(offsetof(Settings, member)), uint16_t(sizeof(Settings::member)), lo, hi, help}

// Nested member sizes for array elements.
#define CSTR_FIELD(key, id, member, sz, help) \
  {key, id, FieldType::Str, uint16_t(offsetof(Settings, member)), uint16_t(sz), 0, 0, help}

#define CONTACT(n)                                                                            \
  CSTR_FIELD("contact" #n ".label", uint16_t(0x100 + ((n)-1) * 2), profile.contacts[(n)-1].label, \
             sizeof(ContactLine::label), "label shown before the value, e.g. Email"),         \
  CSTR_FIELD("contact" #n ".value", uint16_t(0x101 + ((n)-1) * 2), profile.contacts[(n)-1].value, \
             sizeof(ContactLine::value), "contact detail; empty hides the line"),        \
  CSTR_FIELD("contact" #n ".type", uint16_t(0x140 + ((n)-1)), profile.contacts[(n)-1].type,          \
             sizeof(ContactLine::type), "email|phone|web|github|discord|text; github/discord show an icon")

#define PROJECT(n)                                                                              \
  CSTR_FIELD("project" #n ".title", uint16_t(0x200 + ((n)-1) * 8), profile.projects[(n)-1].title, \
             sizeof(Project::title), "project name; empty hides the project"),                  \
  CSTR_FIELD("project" #n ".tagline", uint16_t(0x201 + ((n)-1) * 8),                            \
             profile.projects[(n)-1].tagline, sizeof(Project::tagline), "one-line summary"),    \
  CSTR_FIELD("project" #n ".body", uint16_t(0x202 + ((n)-1) * 8), profile.projects[(n)-1].body, \
             sizeof(Project::body), "wrapped description (\\n for a line break)"),              \
  CSTR_FIELD("project" #n ".link", uint16_t(0x203 + ((n)-1) * 8), profile.projects[(n)-1].link, \
             sizeof(Project::link), "https:// repository URL; enables hold-B project QR"),     \
  CSTR_FIELD("project" #n ".status", uint16_t(0x204 + ((n)-1) * 8),                             \
             profile.projects[(n)-1].status, sizeof(Project::status), "verified status label"), \
  CSTR_FIELD("project" #n ".banner", uint16_t(0x205 + ((n)-1) * 8),                             \
             profile.projects[(n)-1].banner, sizeof(Project::banner), "prominent banner (teaser)")

const FieldDesc kFields[] = {
    STR_FIELD("name", 0x001, profile.name, "name shown on the badge"),
    STR_FIELD("title", 0x002, profile.title, "role / subtitle"),
    STR_FIELD("affiliation", 0x003, profile.affiliation, "company or group; optional"),
    STR_FIELD("interests", 0x004, profile.interests, "interests line; optional"),
    STR_FIELD("event", 0x005, profile.event, "event tag, e.g. Formnext 2026; optional"),
    CONTACT(1), CONTACT(2), CONTACT(3), CONTACT(4), CONTACT(5), CONTACT(6),
    STR_FIELD("qr.payload", 0x010, profile.qr_payload, "QR content: https:// URL or BEGIN:VCARD...; empty = not configured"),
    STR_FIELD("qr.caption", 0x011, profile.qr_caption, "text next to the QR code"),
    PROJECT(1), PROJECT(2), PROJECT(3), PROJECT(4), PROJECT(5), PROJECT(6),
    PROJECT(7), PROJECT(8), PROJECT(9), PROJECT(10), PROJECT(11), PROJECT(12),
    NUM_FIELD("layout", 0x300, FieldType::U8, prefs.layout, 0, 1, "0 = portrait left, 1 = portrait right"),
    NUM_FIELD("refresh.speed", 0x301, FieldType::U8, prefs.refresh_speed, 0, 3, "0 slow/clean .. 3 turbo"),
    NUM_FIELD("refresh.partial", 0x302, FieldType::Bool, prefs.partial_refresh, 0, 1, "allow partial refresh"),
    NUM_FIELD("refresh.max_partials", 0x303, FieldType::U8, prefs.max_partials, 0, 20, "partials before a full refresh"),
    NUM_FIELD("sleep.timeout_s", 0x304, FieldType::U16, prefs.sleep_timeout_s, 0, 3600, "battery auto power-off; 0 = never (min 15)"),
    NUM_FIELD("sleep.screen", 0x305, FieldType::U8, prefs.sleep_screen, 0, 1, "0 keep current screen, 1 show badge before power-off"),
    NUM_FIELD("wake.selects_screen", 0x306, FieldType::Bool, prefs.wake_selects_screen, 0, 1, "wake button picks the first screen"),
    NUM_FIELD("diag.single_core", 0x307, FieldType::Bool, prefs.single_core, 0, 1, "run the display service on core 0 (next boot)"),
    NUM_FIELD("led.level", 0x308, FieldType::U8, prefs.led_level, 0, 255, "activity LED brightness while refreshing"),
    NUM_FIELD("battery.low_mv", 0x309, FieldType::U16, prefs.battery_low_mv, 0, 4500, "show LOW below this (LiPo); 0 = off"),
    NUM_FIELD("battery.bar1_mv", 0x30A, FieldType::U16, prefs.battery_bar_mv[0], 3000, 4500, "1 bar at or above"),
    NUM_FIELD("battery.bar2_mv", 0x30B, FieldType::U16, prefs.battery_bar_mv[1], 3000, 4500, "2 bars at or above"),
    NUM_FIELD("battery.bar3_mv", 0x30C, FieldType::U16, prefs.battery_bar_mv[2], 3000, 4500, "3 bars at or above"),
    NUM_FIELD("battery.bar4_mv", 0x30D, FieldType::U16, prefs.battery_bar_mv[3], 3000, 4500, "4 bars at or above"),
    NUM_FIELD("battery.hyst_mv", 0x30E, FieldType::U16, prefs.battery_hyst_mv, 0, 300, "hysteresis around thresholds"),
    NUM_FIELD("battery.cal_permille", 0x30F, FieldType::U16, prefs.battery_cal_permille, 900, 1100, "scale readings to match a multimeter (1000 = none)"),
    NUM_FIELD("gesture.default_on", 0x310, FieldType::Bool, prefs.gesture_default_on, 0, 1, "gesture mode on after boot"),
    NUM_FIELD("gesture.rotation", 0x311, FieldType::U8, prefs.gesture_rotation, 0, 3, "sensor mounting rotation x 90 deg clockwise"),
    NUM_FIELD("gesture.mirror", 0x312, FieldType::Bool, prefs.gesture_mirror, 0, 1, "swap left/right (sensor facing the other way)"),
    NUM_FIELD("gesture.sensitivity", 0x313, FieldType::U8, prefs.gesture_sensitivity, 10, 90, "minimum swipe strength; higher = fewer false swipes"),
    NUM_FIELD("gesture.timeout_s", 0x314, FieldType::U16, prefs.gesture_timeout_s, 0, 3600, "gesture mode auto-off after idle; 0 = never"),
    NUM_FIELD("gesture.cooldown_ms", 0x315, FieldType::U16, prefs.gesture_cooldown_ms, 200, 3000, "one swipe per this interval"),
};

constexpr size_t kFieldCount = sizeof(kFields) / sizeof(kFields[0]);

bool parse_uint(const char *s, uint32_t *out) {
  if (!s || !*s) return false;
  uint32_t v = 0;
  for (const char *p = s; *p; ++p) {
    if (*p < '0' || *p > '9') return false;
    v = v * 10 + uint32_t(*p - '0');
    if (v > 100000) return false;
  }
  *out = v;
  return true;
}

const char *const kContactTypes[] = {"", "email", "phone", "web", "github", "discord", "text"};

bool is_contact_type_field(uint16_t id) { return id >= 0x140 && id < 0x140 + kMaxContacts; }
bool is_project_link_field(uint16_t id) {
  return id >= 0x200 && id < 0x200 + 8 * kMaxProjects && (id - 0x200) % 8 == 3;
}

}  // namespace

ContactType contact_type(const char *t) {
  for (size_t i = 1; i < sizeof kContactTypes / sizeof kContactTypes[0]; ++i)
    if (std::strcmp(t, kContactTypes[i]) == 0) return ContactType(i);
  return ContactType::None;
}

const char *const *contact_type_names(size_t *count) {
  *count = sizeof kContactTypes / sizeof kContactTypes[0];
  return kContactTypes;
}

bool settings_text_ok(const FieldDesc &f, const char *s, size_t len) {
  if (is_contact_type_field(f.id)) {
    if (len == 0) return true;
    for (const char *t : kContactTypes)
      if (std::strlen(t) == len && std::memcmp(t, s, len) == 0) return true;
    return false;
  }
  if (is_project_link_field(f.id)) {
    if (len == 0) return true;
    // A URL for a QR code: https only, a host part, no spaces or line breaks.
    if (len <= 8 || std::strncmp(s, "https://", 8) != 0) return false;
    for (size_t i = 8; i < len; ++i)
      if (s[i] == ' ' || s[i] == '\n') return false;
    return s[8] != '/';
  }
  return true;
}

const FieldDesc *settings_fields(size_t *count) {
  *count = kFieldCount;
  return kFields;
}

const FieldDesc *find_field(const char *key) {
  for (const auto &f : kFields)
    if (std::strcmp(f.key, key) == 0) return &f;
  return nullptr;
}

const FieldDesc *find_field_id(uint16_t id) {
  for (const auto &f : kFields)
    if (f.id == id) return &f;
  return nullptr;
}

const char *set_result_str(SetResult r) {
  switch (r) {
    case SetResult::Ok: return "ok";
    case SetResult::UnknownKey: return "unknown key";
    case SetResult::TooLong: return "value too long";
    case SetResult::BadUtf8: return "invalid UTF-8 or control character";
    case SetResult::OutOfRange: return "value out of range";
    case SetResult::BadNumber: return "not a number";
    case SetResult::BadValue: return "invalid value for this field";
  }
  return "?";
}

SetResult settings_set(Settings *s, const FieldDesc &f, const char *value) {
  uint8_t *base = reinterpret_cast<uint8_t *>(s) + f.offset;
  if (f.type == FieldType::Str) {
    const size_t len = cstr_len(value, f.size);
    if (len >= f.size) return SetResult::TooLong;
    if (!utf8_valid_printable(value, len)) return SetResult::BadUtf8;
    if (!settings_text_ok(f, value, len)) return SetResult::BadValue;
    std::memset(base, 0, f.size);
    std::memcpy(base, value, len);
    return SetResult::Ok;
  }
  uint32_t v;
  if (f.type == FieldType::Bool) {
    if (!std::strcmp(value, "true") || !std::strcmp(value, "on") || !std::strcmp(value, "yes")) v = 1;
    else if (!std::strcmp(value, "false") || !std::strcmp(value, "off") || !std::strcmp(value, "no")) v = 0;
    else if (!parse_uint(value, &v)) return SetResult::BadNumber;
  } else if (!parse_uint(value, &v)) {
    return SetResult::BadNumber;
  }
  if (v < f.min || v > f.max) return SetResult::OutOfRange;
  if (f.id == 0x304 && v != 0 && v < 15) return SetResult::OutOfRange;  // avoid power-off loops
  if (f.size == 1) *base = uint8_t(v);
  else { const uint16_t v16 = uint16_t(v); std::memcpy(base, &v16, 2); }
  return SetResult::Ok;
}

void settings_get(const Settings &s, const FieldDesc &f, char *buf, size_t buflen) {
  if (!buflen) return;
  const uint8_t *base = reinterpret_cast<const uint8_t *>(&s) + f.offset;
  if (f.type == FieldType::Str) {
    const size_t len = cstr_len(reinterpret_cast<const char *>(base), f.size);
    const size_t n = len < buflen - 1 ? len : buflen - 1;
    std::memcpy(buf, base, n);
    buf[n] = 0;
    return;
  }
  uint32_t v;
  if (f.size == 1) v = *base;
  else { uint16_t v16; std::memcpy(&v16, base, 2); v = v16; }
  if (f.type == FieldType::Bool) std::snprintf(buf, buflen, "%s", v ? "true" : "false");
  else std::snprintf(buf, buflen, "%u", unsigned(v));
}

bool settings_validate(const Settings &s, const FieldDesc **first_bad) {
  // Battery thresholds must be strictly increasing, LOW below one bar.
  const uint16_t *b = s.prefs.battery_bar_mv;
  for (int i = 1; i < 4; ++i) {
    if (b[i] <= b[i - 1]) {
      if (first_bad) *first_bad = find_field(i == 1 ? "battery.bar2_mv" : i == 2 ? "battery.bar3_mv" : "battery.bar4_mv");
      return false;
    }
  }
  if (s.prefs.battery_low_mv && s.prefs.battery_low_mv > b[0]) {
    if (first_bad) *first_bad = find_field("battery.low_mv");
    return false;
  }
  for (const auto &f : kFields) {
    const uint8_t *base = reinterpret_cast<const uint8_t *>(&s) + f.offset;
    bool ok = true;
    if (f.type == FieldType::Str) {
      const char *str = reinterpret_cast<const char *>(base);
      const size_t len = cstr_len(str, f.size);
      ok = len < f.size && utf8_valid_printable(str, len) && settings_text_ok(f, str, len);
    } else {
      uint32_t v;
      if (f.size == 1) v = *base;
      else { uint16_t v16; std::memcpy(&v16, base, 2); v = v16; }
      ok = v >= f.min && v <= f.max;
      if (f.id == 0x304 && v != 0 && v < 15) ok = false;
    }
    if (!ok) {
      if (first_bad) *first_bad = &f;
      return false;
    }
  }
  return true;
}

void settings_defaults(Settings *s) {
  std::memset(s, 0, sizeof(*s));
  s->prefs.layout = uint8_t(Layout::PortraitLeft);
  s->prefs.refresh_speed = 1;
  s->prefs.partial_refresh = 1;
  s->prefs.max_partials = 5;
  s->prefs.sleep_timeout_s = 120;
  s->prefs.sleep_screen = uint8_t(SleepScreen::Badge);
  s->prefs.wake_selects_screen = 1;
  s->prefs.single_core = 0;
  s->prefs.led_level = 24;
  // Single-cell LiPo, resting voltage under the badge's light load. The
  // mapping to remaining charge is approximate (see docs/BATTERY.md).
  s->prefs.battery_low_mv = 3500;
  s->prefs.battery_bar_mv[0] = 3600;
  s->prefs.battery_bar_mv[1] = 3700;
  s->prefs.battery_bar_mv[2] = 3800;
  s->prefs.battery_bar_mv[3] = 3950;
  s->prefs.battery_hyst_mv = 40;
  s->prefs.battery_cal_permille = 1000;
  s->prefs.gesture_default_on = 0;
  s->prefs.gesture_rotation = 0;
  s->prefs.gesture_mirror = 0;
  s->prefs.gesture_sensitivity = 30;
  s->prefs.gesture_timeout_s = 300;
  s->prefs.gesture_cooldown_ms = 700;
  for (size_t i = 0; i < kDefaultProfileCount; ++i) {
    const FieldDesc *f = find_field(kDefaultProfile[i].key);
    // profilegen.py validates keys and lengths; a mismatch here is a build bug
    // and is caught by the host test suite. Skip rather than corrupt memory.
    if (f) (void)settings_set(s, *f, kDefaultProfile[i].value);
  }
}

int configured_project_count(const Profile &p) {
  int n = 0;
  for (const auto &pr : p.projects)
    if (!str_empty(pr.title)) ++n;
  return n;
}

int nth_configured_project(const Profile &p, int n) {
  for (int i = 0; i < kMaxProjects; ++i) {
    if (!str_empty(p.projects[i].title)) {
      if (n == 0) return i;
      --n;
    }
  }
  return -1;
}

bool qr_configured(const Profile &p) { return !str_empty(p.qr_payload); }

const char *project_url(const Profile &p, int n) {
  const int i = nth_configured_project(p, n);
  if (i < 0) return nullptr;
  const char *l = p.projects[i].link;
  return std::strncmp(l, "https://", 8) == 0 && l[8] ? l : nullptr;
}

}  // namespace badge
