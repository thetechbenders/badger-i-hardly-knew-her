#include <string>

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
  // Unconfigured: no symbol, clearly marked placeholder.
  set("qr.payload", "");
  RenderContext c = ctx_with();
  CHECK(card_geometry(c, false).qr_status == QrStatus::Empty);
  // Too long for the panel.
  set("qr.payload", ("https://example.com/" + std::string(360, 'x')).c_str());
  CHECK(card_geometry(c, false).qr_status == QrStatus::TooLong);
}
