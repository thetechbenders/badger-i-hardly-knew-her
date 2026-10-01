// Badge content/profile and preferences, plus the field table that drives
// the serial CLI, the flash codec and validation.
#pragma once

#include <cstddef>
#include <cstdint>

namespace badge {

constexpr int kMaxContacts = 6;
constexpr int kMaxProjects = 12;  // bounded portfolio; v1 records held 4

// Explicit contact kinds (contactN.type). Empty = untyped text line with its
// label, which is how every pre-type profile renders.
enum class ContactType : uint8_t { None = 0, Email, Phone, Web, GitHub, Discord, Text };
ContactType contact_type(const char *type_field);
const char *const *contact_type_names(size_t *count);

struct ContactLine {
  char label[16];
  char value[72];
  char type[12];  // "", email, phone, web, github, discord, text
};

struct Project {
  char title[40];    // empty = slot unused
  char tagline[64];  // optional one-line subtitle
  char body[200];    // short description, wrapped
  char link[72];     // optional https:// URL (repository); enables the project QR
  char status[48];   // optional, verified status label
  char banner[40];   // optional prominent banner (teasers), e.g. "TOP SECRET - COMING SOON"
};

struct Profile {
  char name[48];
  char title[56];
  char affiliation[56];
  char interests[112];
  char event[32];
  ContactLine contacts[kMaxContacts];
  char qr_payload[384];
  char qr_caption[40];
  Project projects[kMaxProjects];
};

enum class Layout : uint8_t { PortraitLeft = 0, PortraitRight = 1 };
enum class SleepScreen : uint8_t { Keep = 0, Badge = 1 };

struct Prefs {
  uint8_t layout;              // Layout
  uint8_t refresh_speed;       // 0 = OTP LUT (slowest, cleanest) .. 3 = turbo
  uint8_t partial_refresh;     // bool: allow partial updates for small changes
  uint8_t max_partials;        // partial updates before a forced full refresh
  uint16_t sleep_timeout_s;    // battery only; 0 = never auto power off
  uint8_t sleep_screen;        // SleepScreen
  uint8_t wake_selects_screen; // bool: the wake button picks the first screen
  uint8_t single_core;         // bool: diagnostic single-core display mode
  uint8_t led_level;           // activity LED brightness during refresh, 0..255
  uint16_t battery_low_mv;     // "LOW" below this (single-cell LiPo); 0 = never
  uint16_t battery_bar_mv[4];  // 1..4 bars at or above these voltages
  uint16_t battery_hyst_mv;    // hysteresis around every threshold
  uint16_t battery_cal_permille;  // multimeter calibration factor (1000 = none)
  uint8_t gesture_default_on;  // gesture mode state after boot
  uint8_t gesture_rotation;    // sensor mounting rotation, 90 degree steps clockwise
  uint8_t gesture_mirror;      // swap left/right after rotation
  uint8_t gesture_sensitivity; // minimum ratio change (percent) for a swipe
  uint16_t gesture_timeout_s;  // gesture mode switches itself off after this idle time; 0 = never
  uint16_t gesture_cooldown_ms;  // ignore further swipes for this long after one
};

struct Settings {
  Profile profile;
  Prefs prefs;
};

enum class FieldType : uint8_t { Str, U8, U16, Bool };

struct FieldDesc {
  const char *key;    // CLI name, e.g. "contact1.value"
  uint16_t id;        // stable on-flash TLV id; never reuse
  FieldType type;
  uint16_t offset;    // offset within Settings
  uint16_t size;      // bytes of storage (string capacity incl. NUL)
  uint16_t min, max;  // numeric bounds (inclusive)
  const char *help;
};

// All fields, in display order.
const FieldDesc *settings_fields(size_t *count);
const FieldDesc *find_field(const char *key);
const FieldDesc *find_field_id(uint16_t id);

// Factory defaults (compiled-in profile from config/*.json plus prefs).
void settings_defaults(Settings *s);

enum class SetResult : uint8_t { Ok, UnknownKey, TooLong, BadUtf8, OutOfRange, BadNumber, BadValue };
const char *set_result_str(SetResult r);

// Parse and validate `value` for `f`, then store into `s`.
SetResult settings_set(Settings *s, const FieldDesc &f, const char *value);
// Field-specific content rules for text fields (contact types, https:// project
// links). Shared by settings_set, settings_validate and the flash decoder.
bool settings_text_ok(const FieldDesc &f, const char *s, size_t len);
// Format the value of `f` into buf (always NUL terminated).
void settings_get(const Settings &s, const FieldDesc &f, char *buf, size_t buflen);
// Validate every field of an in-memory settings object (bounds, UTF-8, NUL).
bool settings_validate(const Settings &s, const FieldDesc **first_bad);

// Convenience accessors
inline bool str_empty(const char *s) { return s == nullptr || s[0] == '\0'; }
int configured_project_count(const Profile &p);
int nth_configured_project(const Profile &p, int n);  // index into projects[] or -1
bool qr_configured(const Profile &p);
// Link of the n-th configured project if it is a usable https:// URL, else nullptr.
const char *project_url(const Profile &p, int n);

}  // namespace badge
