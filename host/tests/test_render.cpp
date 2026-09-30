#include <string>
#include <vector>

#include "check.hpp"
#include "renderer.hpp"
#include "text.hpp"

namespace badge {
extern const uint8_t kBuiltinAssetPack[];
extern const size_t kBuiltinAssetPack_size;
}  // namespace badge

using namespace badge;

namespace {
Settings g_s;
Framebuffer g_fb, g_fb2;

RenderContext ctx_with(bool portrait = true) {
  RenderContext c;
  c.settings = &g_s;
  if (portrait) {
    CHECK(asset_pack_validate(kBuiltinAssetPack, kBuiltinAssetPack_size).status == AssetStatus::Ok);
    CHECK(asset_pack_bitmap(kBuiltinAssetPack, kAssetIdPortrait, &c.portrait));
  }
  return c;
}

void set(const char *k, const char *v) { CHECK(settings_set(&g_s, *find_field(k), v) == SetResult::Ok); }

std::string maxlen(const char *key, const char *unit) {
  const FieldDesc *f = find_field(key);
  std::string s;
  while (s.size() + std::strlen(unit) < f->size) s += unit;
  return s;
}

// Every portrait pixel must be exactly the bitmap: text never overlaps it.
bool portrait_intact(const Framebuffer &fb, const MonoBitmap &p, int x0, int y0) {
  for (int y = 0; y < p.height; ++y)
    for (int x = 0; x < p.width; ++x) {
      const bool on = p.bits[y * p.stride + (x >> 3)] & (0x80 >> (x & 7));
      if ((fb.get(x0 + x, y0 + y) == Ink::Black) != on) return false;
    }
  return true;
}

bool region_white(const Framebuffer &fb, Rect r) {
  for (int x = r.x; x < r.right(); ++x)
    for (int y = r.y; y < r.bottom(); ++y)
      if (fb.get(x, y) == Ink::Black) return false;
  return true;
}
}  // namespace

TEST(render_all_screens_with_defaults) {
  settings_defaults(&g_s);
  RenderContext c = ctx_with();
  InfoLines info;
  info.add("line %d", 1);
  c.info = &info;
  for (uint8_t s = 0; s < uint8_t(Screen::Count); ++s) {
    View v;
    v.screen = Screen(s);
    render(g_fb, v, c);
    CHECK(g_fb.hash() != Framebuffer().hash());  // something visible on every screen
  }
}

TEST(render_worst_case_text_stays_in_bounds) {
  settings_defaults(&g_s);
  // Wide glyphs at the maximum byte length for every text field.
  size_t n;
  const FieldDesc *f = settings_fields(&n);
  for (size_t i = 0; i < n; ++i)
    if (f[i].type == FieldType::Str && std::strcmp(f[i].key, "qr.payload") != 0)
      set(f[i].key, maxlen(f[i].key, "W").c_str());
  set("qr.payload", "https://example.com/c");
  for (uint8_t layout = 0; layout < 2; ++layout) {
    set("layout", layout ? "1" : "0");
    RenderContext c = ctx_with();
    View v;
    v.layout = layout;
    render(g_fb, v, c);
    const int px = layout ? 296 - c.portrait.width : 0;
    CHECK(portrait_intact(g_fb, c.portrait, px, (128 - c.portrait.height) / 2));
  }
  RenderContext c = ctx_with();
  View v;
  v.screen = Screen::Card;
  render(g_fb, v, c);
  CardGeometry g = card_geometry(c, false);
  CHECK(g.qr_status == QrStatus::Ok);
  // Text must not intrude into the QR symbol or its quiet zone.
  static Framebuffer qr_only;
  qr_only.clear(Ink::White);
  QrSymbol sym;
  qr_encode(g_s.profile.qr_payload, 128, &sym);
  qr_draw(qr_only, sym, g.qr.x, g.qr.y);
  for (int x = g.qr.x; x < g.qr.right(); ++x)
    for (int y = g.qr.y; y < g.qr.bottom(); ++y) CHECK(g_fb.get(x, y) == qr_only.get(x, y));
  // Multi-byte text at the limit also renders without overrunning.
  set("name", maxlen("name", "\xC3\x9C").c_str());
  v.screen = Screen::Badge;
  render(g_fb, v, c);
}

TEST(render_missing_optional_fields_disappear) {
  settings_defaults(&g_s);
  RenderContext c = ctx_with();
  View v;
  v.screen = Screen::Card;
  // A contact with a label but no value renders exactly like no contact.
  for (int i = 1; i <= 4; ++i) {
    set(("contact" + std::to_string(i) + ".label").c_str(), "");
    set(("contact" + std::to_string(i) + ".value").c_str(), "");
  }
  set("contact1.value", "a@b.c");
  render(g_fb, v, c);
  set("contact2.label", "Phone");
  render(g_fb2, v, c);
  CHECK(g_fb.equals(g_fb2));
  // Empty affiliation/interests/event: badge column simply shrinks.
  v.screen = Screen::Badge;
  set("affiliation", "");
  set("interests", "");
  set("event", "");
  render(g_fb, v, c);
  CHECK(region_white(g_fb, {110, 60, 186, 68}));  // nothing below the title block
}

TEST(render_without_portrait_uses_full_width) {
  settings_defaults(&g_s);
  RenderContext c = ctx_with(false);
  View v;
  render(g_fb, v, c);
  CHECK(!region_white(g_fb, {0, 0, 60, 60}));  // name starts near the left edge
}

TEST(qr_geometry_integer_modules_and_quiet_zone) {
  settings_defaults(&g_s);
  const char *payloads[] = {"https://example.com/dan",
                            "https://example.org/a/really/quite/long/path/for/a/contact/page?x=1",
                            "BEGIN:VCARD\nVERSION:3.0\nN:Brown;Dan;;;\nFN:Dan Brown\nTITLE:Engineer\nEMAIL:dan@example.com\nURL:https://example.com\nEND:VCARD"};
  for (const char *p : payloads) {
    set("qr.payload", p);
    for (int full = 0; full < 2; ++full) {
      RenderContext c = ctx_with();
      CardGeometry g = card_geometry(c, full);
      CHECK(g.qr_status == QrStatus::Ok);
      CHECK(g.qr_scale >= kQrMinScale);
      CHECK(g.qr.x >= 0 && g.qr.y >= 0 && g.qr.right() <= 296 && g.qr.bottom() <= 128);
      View v;
      v.screen = full ? Screen::QrFull : Screen::Card;
      render(g_fb, v, c);
      // Quiet zone: 4 modules of white on every side of the symbol.
      const int q = kQrQuietModules * g.qr_scale;
      CHECK(region_white(g_fb, {g.qr.x, g.qr.y, g.qr.w, int16_t(q)}));
      CHECK(region_white(g_fb, {g.qr.x, int16_t(g.qr.bottom() - q), g.qr.w, int16_t(q)}));
      CHECK(region_white(g_fb, {g.qr.x, g.qr.y, int16_t(q), g.qr.h}));
      CHECK(region_white(g_fb, {int16_t(g.qr.right() - q), g.qr.y, int16_t(q), g.qr.h}));
      // Finder pattern corner is black at an exact module boundary.
      CHECK(g_fb.get(g.qr.x + q, g.qr.y + q) == Ink::Black);
      CHECK(g_fb.get(g.qr.x + q - 1, g.qr.y + q) == Ink::White);
    }
  }
  // Unconfigured: no symbol and no placeholder (render_unconfigured_qr_...).
  set("qr.payload", "");
  RenderContext c = ctx_with();
  CHECK(card_geometry(c, false).qr_status == QrStatus::Empty);
  // Too long for the panel.
  set("qr.payload", ("https://example.com/" + std::string(360, 'x')).c_str());
  CHECK(card_geometry(c, false).qr_status == QrStatus::TooLong);
}

namespace {
StatusInfo status_of(PowerDisplay d, uint8_t bars, bool low, GestureIndicator g) {
  StatusInfo s;
  s.battery.display = d;
  s.battery.bars = bars;
  s.battery.low = low;
  s.gesture = g;
  return s;
}
}  // namespace

TEST(render_status_area_reserved_on_every_screen) {
  settings_defaults(&g_s);
  size_t n;
  const FieldDesc *f = settings_fields(&n);
  for (int worst = 0; worst < 2; ++worst) {
    if (worst) {
      for (size_t i = 0; i < n; ++i)
        if (f[i].type == FieldType::Str && std::strcmp(f[i].key, "qr.payload") != 0)
          set(f[i].key, maxlen(f[i].key, "\xC3\x9C").c_str());  // tall accented capitals
      set("qr.payload", "https://example.com/c");
    }
    InfoLines info;
    for (int i = 0; i < InfoLines::kMax; ++i) info.add("%s", std::string(80, 'W').c_str());
    for (uint8_t sc = 0; sc < uint8_t(Screen::Count); ++sc) {
      for (uint8_t layout = 0; layout < 2; ++layout) {
        for (int portrait = 0; portrait < 2; ++portrait) {
          RenderContext c = ctx_with(portrait);
          c.info = &info;
          View v;
          v.screen = Screen(sc);
          v.layout = layout;
          render(g_fb, v, c);  // status Unknown/Off: nothing drawn there
          const Rect sr = status_rect(v, c);
          CHECK(sr.x >= 0 && sr.right() <= 296);
          if (v.screen == Screen::Recovery) {
            for (int x = sr.x; x < sr.right(); ++x)
              for (int y = sr.y; y < sr.bottom(); ++y) CHECK(g_fb.get(x, y) == Ink::Black);  // inside the header
          } else {
            CHECK(region_white(g_fb, sr));
          }
          // Drawing the fullest status changes nothing outside its rectangle.
          c.status = status_of(PowerDisplay::Battery, 0, true, GestureIndicator::Fault);
          render(g_fb2, v, c);
          for (int x = 0; x < 296; ++x)
            for (int y = 0; y < 128; ++y) {
              const bool inside = x >= sr.x && x < sr.right() && y >= sr.y && y < sr.bottom();
              if (!inside) CHECK(g_fb.get(x, y) == g_fb2.get(x, y));
            }
          CHECK(!g_fb.equals(g_fb2));
        }
      }
    }
  }
}

TEST(render_status_states_are_distinct) {
  settings_defaults(&g_s);
  RenderContext c = ctx_with();
  View v;
  const StatusInfo states[] = {
      status_of(PowerDisplay::Unknown, 0, false, GestureIndicator::Off),
      status_of(PowerDisplay::Usb, 0, false, GestureIndicator::Off),
      status_of(PowerDisplay::Invalid, 0, false, GestureIndicator::Off),
      status_of(PowerDisplay::Battery, 0, true, GestureIndicator::Off),
      status_of(PowerDisplay::Battery, 0, false, GestureIndicator::Off),
      status_of(PowerDisplay::Battery, 1, false, GestureIndicator::Off),
      status_of(PowerDisplay::Battery, 2, false, GestureIndicator::Off),
      status_of(PowerDisplay::Battery, 3, false, GestureIndicator::Off),
      status_of(PowerDisplay::Battery, 4, false, GestureIndicator::Off),
      status_of(PowerDisplay::Battery, 4, false, GestureIndicator::On),
      status_of(PowerDisplay::Battery, 4, false, GestureIndicator::Fault),
  };
  std::vector<uint32_t> hashes;
  for (const auto &st : states) {
    c.status = st;
    render(g_fb, v, c);
    const uint32_t h = g_fb.hash();
    for (uint32_t o : hashes) CHECK(o != h);
    hashes.push_back(h);
  }
}

TEST(render_card_six_contacts_with_vcard) {
  settings_defaults(&g_s);
  const char *vals[6] = {"work@example.com", "person@example.com", "+1 (555) 010-0000", "handle", "user_name", "x"};
  const char *labels[6] = {"Work", "Email", "Phone", "GitHub", "Discord", "Extra"};
  for (int i = 0; i < 6; ++i) {
    set(("contact" + std::to_string(i + 1) + ".label").c_str(), labels[i]);
    set(("contact" + std::to_string(i + 1) + ".value").c_str(), vals[i]);
  }
  set("qr.payload", "BEGIN:VCARD\nVERSION:3.0\nN:Brown;Dan;;;\nFN:Dan Brown\nEND:VCARD");
  RenderContext c = ctx_with();
  View v;
  v.screen = Screen::Card;
  render(g_fb, v, c);
  CHECK(card_geometry(c, false).qr_status == QrStatus::Ok);
}

namespace {
void clear_contacts() {
  for (int i = 1; i <= kMaxContacts; ++i) {
    set(("contact" + std::to_string(i) + ".label").c_str(), "");
    set(("contact" + std::to_string(i) + ".value").c_str(), "");
  }
}
}  // namespace

// No payload: nothing QR-like is drawn (no dashed box, no "not configured"
// text) and the contact column takes the width the code would have used.
TEST(render_unconfigured_qr_leaves_no_trace) {
  settings_defaults(&g_s);
  clear_contacts();
  set("name", "Ada");
  set("title", "Engineer");
  set("contact1.label", "Email");
  set("contact1.value", "a@b.c");
  set("qr.payload", "");
  set("qr.caption", "Scan me");
  RenderContext c = ctx_with();
  View v;
  v.screen = Screen::Card;
  render(g_fb, v, c);
  CHECK(region_white(g_fb, {150, 0, 146, 128}));  // right part of the card is empty
  CHECK(status_rect(v, c).right() == 294);        // status back in the top-right corner
  // The full-screen QR request falls back to the card.
  v.screen = Screen::QrFull;
  render(g_fb2, v, c);
  CHECK(g_fb.equals(g_fb2));
  // Width is reclaimed: a long value runs past where the code would start.
  set("contact1.value", maxlen("contact1.value", "W").c_str());
  v.screen = Screen::Card;
  render(g_fb, v, c);
  CHECK(!region_white(g_fb, {200, 20, 80, 90}));
}

// With every contact empty the card shows identity only: no placeholder
// sentence, no orphaned labels.
TEST(render_card_without_contacts_is_clean) {
  settings_defaults(&g_s);
  clear_contacts();
  set("contact3.label", "Phone");  // label without a value hides the line
  set("qr.payload", "");
  set("affiliation", "");
  RenderContext c = ctx_with();
  View v;
  v.screen = Screen::Card;
  render(g_fb, v, c);
  CHECK(region_white(g_fb, {0, 60, 296, 68}));  // nothing below the name/title/rule block
}

// Maximum-length content with wrapped titles, every status state and a
// fault message still keeps every screen inside the panel and the status
// area; host-only sample, the strings are clearly synthetic.
TEST(render_maximum_content_all_screens_and_states) {
  settings_defaults(&g_s);
  size_t n;
  const FieldDesc *f = settings_fields(&n);
  for (size_t i = 0; i < n; ++i)
    if (f[i].type == FieldType::Str && std::strcmp(f[i].key, "qr.payload") != 0)
      set(f[i].key, maxlen(f[i].key, "Wi ").c_str());  // breakable, so titles wrap
  set("qr.payload", "https://example.com/c");
  InfoLines info;
  info.add("Last      hard fault pc=10001234 lr=10005678 core1");
  info.add("Display   dual-core | full 9 part 9 skip 9 | TIMEOUTS");
  for (int i = 2; i < InfoLines::kMax; ++i) info.add("%s", std::string(70, 'W').c_str());
  const StatusInfo states[] = {
      status_of(PowerDisplay::Usb, 0, false, GestureIndicator::On),
      status_of(PowerDisplay::Battery, 0, true, GestureIndicator::Fault),
      status_of(PowerDisplay::Invalid, 0, false, GestureIndicator::Off),
  };
  for (uint8_t sc = 0; sc < uint8_t(Screen::Count); ++sc)
    for (uint8_t layout = 0; layout < 2; ++layout)
      for (const StatusInfo &st : states) {
        RenderContext c = ctx_with();
        c.info = &info;
        c.recovery_reason = "repeated crashes";
        c.status = st;
        View v;
        v.screen = Screen(sc);
        v.layout = layout;
        v.project = 1;
        render(g_fb, v, c);  // ASan/UBSan catch any overrun
        CHECK(g_fb.hash() != Framebuffer().hash());
        if (v.screen == Screen::Badge) {
          const int px = layout ? 296 - c.portrait.width : 0;
          CHECK(portrait_intact(g_fb, c.portrait, px, (128 - c.portrait.height) / 2));
        }
      }
}

// The end of a realistic project tagline and body must be visible: if the
// renderer ellipsized them, changing the last character would not change
// the frame. (Regression: the Dragon-family tagline was cut to "validati...".)
TEST(render_project_page_shows_full_tagline_and_body) {
  settings_defaults(&g_s);
  const std::string tagline = "Tools for embedded development and validation.";
  const std::string body = "DragonBreath \xC2\xB7 DragonSniff \xC2\xB7 DragonBench \xC2\xB7 dragon-core.";
  set("project1.title", "Jump Jet");
  set("project2.title", "Dragon family");
  set("project2.tagline", tagline.c_str());
  set("project2.body", body.c_str());
  RenderContext c = ctx_with();
  View v;
  v.screen = Screen::Projects;
  v.project = 1;
  render(g_fb, v, c);
  set("project2.tagline", (tagline.substr(0, tagline.size() - 2) + "X.").c_str());
  render(g_fb2, v, c);
  CHECK(!g_fb.equals(g_fb2));
  set("project2.tagline", tagline.c_str());
  set("project2.body", (body.substr(0, body.size() - 2) + "X.").c_str());
  render(g_fb2, v, c);
  CHECK(!g_fb.equals(g_fb2));
}
