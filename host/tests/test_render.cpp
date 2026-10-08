#include <string>
#include <vector>

#include "check.hpp"
#include "icons.hpp"
#include "renderer.hpp"
#include "text.hpp"

namespace badge {
extern const uint8_t kBuiltinAssetPack[];
extern const size_t kBuiltinAssetPack_size;
}  // namespace badge

using namespace badge;

namespace {
constexpr int W = Framebuffer::kWidth, H = Framebuffer::kHeight;
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
  // Fields with content rules get their worst *valid* value.
  if (std::strstr(key, ".type")) return "discord";
  if (std::strstr(key, ".link")) {
    s = "https://github.com/";
    while (s.size() + 1 < f->size) s += 'w';
    return s;
  }
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
    const int px = layout ? W - c.portrait.width : 0;
    CHECK(portrait_intact(g_fb, c.portrait, px, (H - c.portrait.height) / 2));
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
  qr_encode(g_s.profile.qr_payload, target::kQrCardMaxPx, &sym);
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
  CHECK(region_white(g_fb, {110, int16_t(H - 68), W - 110, 68}));  // nothing below the title block
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
  const char *payloads[] = {"https://example.com/alex",
                            "https://example.org/a/really/quite/long/path/for/a/contact/page?x=1",
                            "BEGIN:VCARD\nVERSION:3.0\nN:Example;Alex;;;\nFN:Alex Example\nTITLE:Engineer\nEMAIL:alex@example.com\nURL:https://example.com\nEND:VCARD"};
  for (const char *p : payloads) {
    set("qr.payload", p);
    for (int full = 0; full < 2; ++full) {
      RenderContext c = ctx_with();
      CardGeometry g = card_geometry(c, full);
      CHECK(g.qr_status == QrStatus::Ok);
      CHECK(g.qr_scale >= kQrMinScale);
      CHECK(g.qr.x >= 0 && g.qr.y >= 0 && g.qr.right() <= W && g.qr.bottom() <= H);
      CHECK(g.qr.w <= (full ? target::kQrFullMaxPx : target::kQrCardMaxPx));
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
          CHECK(sr.x >= 0 && sr.right() <= W);
          if (v.screen == Screen::Recovery) {
            for (int x = sr.x; x < sr.right(); ++x)
              for (int y = sr.y; y < sr.bottom(); ++y) CHECK(g_fb.get(x, y) == Ink::Black);  // inside the header
          } else {
            CHECK(region_white(g_fb, sr));
          }
          // Drawing the fullest status changes nothing outside its rectangle.
          c.status = status_of(PowerDisplay::Battery, 0, true, GestureIndicator::Fault);
          render(g_fb2, v, c);
          for (int x = 0; x < W; ++x)
            for (int y = 0; y < H; ++y) {
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
  set("qr.payload", "BEGIN:VCARD\nVERSION:3.0\nN:Example;Alex;;;\nFN:Alex Example\nEND:VCARD");
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
  CHECK(region_white(g_fb, {150, 0, W - 150, H}));  // right part of the card is empty
  CHECK(status_rect(v, c).right() == W - 2);        // status back in the top-right corner
  // The full-screen QR request falls back to the card.
  v.screen = Screen::QrFull;
  render(g_fb2, v, c);
  CHECK(g_fb.equals(g_fb2));
  // Width is reclaimed: a long value runs past where the code would start.
  set("contact1.value", maxlen("contact1.value", "W").c_str());
  v.screen = Screen::Card;
  render(g_fb, v, c);
  CHECK(!region_white(g_fb, {200, 20, W - 216, 90}));
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
  CHECK(region_white(g_fb, {0, 60, W, H - 60}));  // nothing below the name/title/rule block
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
          const int px = layout ? W - c.portrait.width : 0;
          CHECK(portrait_intact(g_fb, c.portrait, px, (H - c.portrait.height) / 2));
        }
      }
}

// The end of a realistic project tagline and body must be visible: if the
// renderer ellipsized them, changing the last character would not change
// the frame. (Regression: a long sample tagline was cut to "validati...".)
TEST(render_project_page_shows_full_tagline_and_body) {
  settings_defaults(&g_s);
  const std::string tagline = "Tools for embedded development and validation.";
  const std::string body = "Bench Logger \xC2\xB7 Load Tester \xC2\xB7 Shared Components \xC2\xB7 Quiet Duct.";
  set("project1.title", "Quiet Duct");
  set("project2.title", "Lab tools");
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

namespace {
// True if the icon's bitmap appears exactly (ink and paper) at (x0, y0).
bool icon_at(const Framebuffer &fb, const Icon &ic, int x0, int y0) {
  for (int y = 0; y < ic.h; ++y)
    for (int x = 0; x < ic.w; ++x) {
      const bool on = ic.bits[y * ic.stride + (x >> 3)] & (0x80 >> (x & 7));
      if ((fb.get(x0 + x, y0 + y) == Ink::Black) != on) return false;
    }
  return true;
}
int find_icon(const Framebuffer &fb, const Icon &ic) {
  int hits = 0;
  for (int y = 0; y + ic.h <= Framebuffer::kHeight; ++y)
    for (int x = 0; x + ic.w <= Framebuffer::kWidth; ++x) hits += icon_at(fb, ic, x, y);
  return hits;
}
void clear_projects() {
  static const char *f[] = {"title", "tagline", "body", "link", "status", "banner"};
  for (int i = 1; i <= kMaxProjects; ++i)
    for (const char *k : f) set(("project" + std::to_string(i) + "." + k).c_str(), "");
}
}  // namespace

TEST(icons_are_small_and_legible) {
  for (const Icon *ic : {&icons::github, &icons::discord}) {
    CHECK_EQ(ic->w, 12);
    CHECK_EQ(ic->h, 12);
    int ink = 0;
    for (int y = 0; y < ic->h; ++y)
      for (int x = 0; x < ic->w; ++x) ink += (ic->bits[y * ic->stride + (x >> 3)] >> (7 - (x & 7))) & 1;
    CHECK(ink > 30 && ink < 120);  // a recognisable glyph, neither empty nor a solid block
  }
}

// Typed GitHub/Discord lines show the icon instead of the platform label;
// untyped (pre-type) profiles keep their text label and get no icon.
TEST(render_card_contact_icons) {
  settings_defaults(&g_s);
  clear_contacts();
  set("qr.payload", "");
  set("contact1.label", "Email");
  set("contact1.value", "a@b.c");
  set("contact1.type", "email");
  set("contact2.label", "GitHub");
  set("contact2.value", "octo");
  set("contact2.type", "github");
  set("contact3.label", "Discord");
  set("contact3.value", "user_name");
  set("contact3.type", "discord");
  RenderContext c = ctx_with();
  View v;
  v.screen = Screen::Card;
  render(g_fb, v, c);
  CHECK_EQ(find_icon(g_fb, icons::github), 1);
  CHECK_EQ(find_icon(g_fb, icons::discord), 1);
  // The icons sit in the label column at the card's left margin, one line apart.
  int gy = -1, dy = -1;
  for (int y = 0; y < H - 12; ++y) {
    if (icon_at(g_fb, icons::github, 8, y)) gy = y;
    if (icon_at(g_fb, icons::discord, 8, y)) dy = y;
  }
  CHECK(gy > 0 && dy == gy + 14);
  // The platform label is not drawn as well: removing it changes nothing.
  set("contact2.label", "");
  set("contact3.label", "");
  render(g_fb2, v, c);
  CHECK(g_fb.equals(g_fb2));
  // Untyped lines (old profiles): text labels, no icon, no guessing from the label.
  set("contact2.label", "GitHub");
  set("contact2.type", "");
  set("contact3.label", "Discord");
  set("contact3.type", "");
  render(g_fb, v, c);
  CHECK_EQ(find_icon(g_fb, icons::github), 0);
  CHECK_EQ(find_icon(g_fb, icons::discord), 0);
  CHECK(!g_fb.equals(g_fb2));
}

TEST(render_teaser_banner_without_link) {
  settings_defaults(&g_s);
  clear_projects();
  set("project1.title", "Secret");
  set("project1.banner", "TOP SECRET - COMING SOON");
  RenderContext c = ctx_with();
  View v;
  v.screen = Screen::Projects;
  render(g_fb, v, c);
  // A solid black band spans the column below the title.
  int band_rows = 0;
  for (int y = 0; y < H; ++y) {
    bool solid = true;
    for (int x = 10; x < 20 && solid; ++x) solid = g_fb.get(x, y) == Ink::Black;
    band_rows += solid;
  }
  CHECK(band_rows >= 12);
  CHECK(project_qr_geometry(c, 0).qr_status == QrStatus::Empty);
  // A QR request for it falls back to the project page: no stale symbol.
  v.screen = Screen::ProjectQr;
  render(g_fb2, v, c);
  CHECK(g_fb.equals(g_fb2));
}

// Every page of a full 12-entry portfolio and every project QR renders
// distinctly (own counter, own payload) with worst-case text.
TEST(render_twelve_projects_distinct_pages_and_qrs) {
  settings_defaults(&g_s);
  clear_projects();
  for (int i = 1; i <= kMaxProjects; ++i) {
    const std::string p = "project" + std::to_string(i);
    set((p + ".title").c_str(), maxlen((p + ".title").c_str(), "Wi ").c_str());
    set((p + ".tagline").c_str(), maxlen((p + ".tagline").c_str(), "Wi ").c_str());
    set((p + ".body").c_str(), maxlen((p + ".body").c_str(), "Wi ").c_str());
    set((p + ".status").c_str(), maxlen((p + ".status").c_str(), "Wi ").c_str());
    set((p + ".link").c_str(), ("https://example.com/p" + std::to_string(i)).c_str());
  }
  RenderContext c = ctx_with();
  std::vector<uint32_t> seen;
  for (uint8_t sc : {uint8_t(Screen::Projects), uint8_t(Screen::ProjectQr)})
    for (int n = 0; n < kMaxProjects; ++n) {
      View v;
      v.screen = Screen(sc);
      v.project = uint8_t(n);
      render(g_fb, v, c);
      const uint32_t h = g_fb.hash();
      for (uint32_t o : seen) CHECK(o != h);
      seen.push_back(h);
      if (v.screen == Screen::ProjectQr) CHECK(project_qr_geometry(c, n).qr_status == QrStatus::Ok);
    }
  // Out-of-range project index never reads past the table.
  View v;
  v.screen = Screen::ProjectQr;
  v.project = 200;
  render(g_fb, v, c);
}

TEST(repo_label_is_compact_and_exact) {
  char b[80];
  const struct { const char *in, *out; } cases[] = {
      {"https://github.com/example-maker/QuietDuct", "example-maker/QuietDuct"},
      {"https://github.com/example-maker/ChamberHeater/", "example-maker/ChamberHeater"},
      {"https://www.github.com/a/b.git", "a/b"},
      {"https://github.com/a/b/tree/main/docs", "a/b"},
      {"https://github.com/a/b?tab=readme", "a/b"},
      {"https://github.com/onlyowner", "github.com/onlyowner"},  // not a repo: keep as is
      {"https://github.com/", "github.com"},
      {"https://example.com/x/", "example.com/x"},
      {"https://gitlab.com/group/proj", "gitlab.com/group/proj"},
      {"", ""},
  };
  for (const auto &t : cases) {
    CHECK_EQ(repo_label(t.in, b, sizeof b), std::strlen(t.out));
    CHECK_STR(b, t.out);
  }
  CHECK_EQ(repo_label("https://github.com/example-maker/QuietDuct", b, 6), 5u);  // bounded
  CHECK_STR(b, "examp");
}

// The configured (sample) portfolio is shown completely: no ellipsis in any
// title, tagline, status, description or link label, and descriptions stay
// at 11 px. Personal profiles get the same check from badger_preview --fit.
TEST(render_sample_portfolio_fits_without_ellipsis) {
  settings_defaults(&g_s);
  RenderContext c = ctx_with();
  const int n = configured_project_count(g_s.profile);
  CHECK(n >= 7);
  for (int i = 0; i < n; ++i) {
    const ProjectFit f = project_fit(g_fb, c, i);
    CHECK(f.title && f.tagline && f.status && f.body && f.link && f.banner && f.qr_title && f.index_title);
    const Project &p = g_s.profile.projects[nth_configured_project(g_s.profile, i)];
    if (!str_empty(p.body)) CHECK_EQ(f.body_px, int(fonts::sans_11.line_height));
    if (!str_empty(p.status)) CHECK(std::strpbrk(p.status, "0123456789") == nullptr);  // no version boxes
  }
  // Configured order: the sample's teaser is sixth and its last entry last.
  CHECK_STR(g_s.profile.projects[nth_configured_project(g_s.profile, 5)].title, "Secret Project");
  CHECK_STR(g_s.profile.projects[nth_configured_project(g_s.profile, n - 1)].title, "Weather Station");
}

// A teaser without a link shows no footer: no URL, no QR glyph, no "hold B".
TEST(render_teaser_has_no_qr_hint) {
  settings_defaults(&g_s);
  clear_projects();
  set("project1.title", "Secret");
  set("project1.banner", "TOP SECRET - COMING SOON");
  set("project2.title", "Other");
  RenderContext c = ctx_with();
  View v;
  v.screen = Screen::Projects;
  render(g_fb, v, c);
  CHECK(region_white(g_fb, {8, H - 14, W - 30, 14}));
  set("project1.link", "https://github.com/a/b");
  render(g_fb, v, c);
  CHECK(!region_white(g_fb, {8, H - 14, W - 30, 14}));
}

// ------------------------------------------------- identity-screen fit report
// screen_fit() feeds the personalisation gate (badger_preview --fit): every
// supplied field must be drawn whole, or the build stops before flashing.

namespace {
bool all_complete(const ScreenFit &f) {
  bool ok = f.name && f.title && f.affiliation && f.interests && f.event && f.caption && f.caption_shown;
  for (int i = 0; i < kMaxContacts; ++i) ok = ok && f.contact_label[i] && f.contact_value[i];
  return ok;
}
}  // namespace

TEST(screen_fit_sample_is_complete_and_draws_like_render) {
  settings_defaults(&g_s);
  set("qr.payload", "https://example.com/alex");
  RenderContext c = ctx_with();
  const struct { Screen s; uint8_t layout; } cases[] = {
      {Screen::Badge, 0}, {Screen::Badge, 1}, {Screen::Card, 0}, {Screen::QrFull, 0}};
  for (const auto &k : cases) {
    CHECK(all_complete(screen_fit(g_fb2, c, k.s, k.layout)));
    // The report only observes: its scratch render equals the real screen.
    View v;
    v.screen = k.s;
    v.layout = k.layout;
    render(g_fb, v, c);
    screen_fit(g_fb2, c, k.s, k.layout);
    CHECK(region_white(g_fb2, status_rect(v, c)));  // no status area in the scratch render
    Framebuffer a;
    a.copy_from(g_fb);
    a.fill_rect(status_rect(v, c), Ink::White);
    CHECK(a.equals(g_fb2));
  }
}

TEST(screen_fit_reports_cut_identity_fields) {
  settings_defaults(&g_s);
  RenderContext c = ctx_with();
  set("name", "Wilhelmina Featherstonehaugh-Cholmondeley");
  ScreenFit f = screen_fit(g_fb, c, Screen::Badge, 0);
  CHECK(!f.name);
  CHECK(f.title && f.interests && f.event);
  CHECK(!screen_fit(g_fb, c, Screen::Card, 0).name);
  settings_defaults(&g_s);
  set("event", "International WWWWWWWWWWWWWWWW");
  CHECK(!screen_fit(g_fb, c, Screen::Badge, 0).event);
  CHECK(!screen_fit(g_fb, c, Screen::Badge, 1).event);
  CHECK(screen_fit(g_fb, c, Screen::Card, 0).event);  // the card does not show the event
  // Interests needing more lines than layout A allows (target::
  // kBadgeInterestLines; layout B's band has two). On the Badger 2040 this
  // list needs four lines; the taller Badger 2350 column takes any natural
  // list that fits the field, so it gets wide filler words instead.
  settings_defaults(&g_s);
  set("interests", target::kBadgeInterestLines <= 3
                       ? "printing, electronics, embedded systems, sensors, firmware, e-paper displays, robotics, metrology"
                       : maxlen("interests", "WWW ").c_str());
  CHECK(!screen_fit(g_fb, c, Screen::Badge, 0).interests);
  CHECK(!screen_fit(g_fb, c, Screen::Badge, 1).interests);
}

TEST(screen_fit_reports_cut_and_dropped_contacts) {
  settings_defaults(&g_s);
  clear_contacts();
  RenderContext c = ctx_with();
  set("contact1.label", "Email");
  set("contact1.value", "a.very.long.address.that.cannot.fit@subdomain.example.com");
  set("contact2.label", "Mobile phone");  // wider than the 52 px label column
  set("contact2.value", "+1 555 0100");
  ScreenFit f = screen_fit(g_fb, c, Screen::Card, 0);
  CHECK(!f.contact_value[0] && f.contact_label[0]);
  CHECK(!f.contact_label[1] && f.contact_value[1]);
  // Typed GitHub lines show the icon, never the label: nothing to cut there.
  set("contact2.label", "Mobile phone");
  set("contact2.type", "github");
  CHECK(screen_fit(g_fb, c, Screen::Card, 0).contact_label[1]);
  // How many lines fit under a two-line title: tests/<target>/test_layout.cpp.
}

TEST(screen_fit_card_caption_is_not_cut) {
  settings_defaults(&g_s);
  RenderContext c = ctx_with();
  set("qr.payload", "https://example.com/alex");
  set("qr.caption", "Scan to save my contact");
  CHECK(all_complete(screen_fit(g_fb, c, Screen::Card, 0)));  // two contacts: room for it
  // Whether it gives way to more contact lines: tests/<target>/test_layout.cpp.
  CHECK(screen_fit(g_fb, c, Screen::QrFull, 0).caption);  // shown in full there
  set("qr.caption", "");
  CHECK(screen_fit(g_fb, c, Screen::Card, 0).caption_shown);  // nothing configured, nothing missing
}

TEST(project_fit_covers_banner_qr_title_and_index_row) {
  settings_defaults(&g_s);
  clear_projects();
  RenderContext c = ctx_with();
  set("project1.title", "Teaser");
  set("project1.banner", "TOP SECRET - COMING SOON");
  set("project2.title", "Linked");
  set("project2.link", "https://github.com/a/b");
  ProjectFit f = project_fit(g_fb, c, 0);
  CHECK(f.banner && f.qr_title && f.index_title);
  f = project_fit(g_fb, c, 1);
  CHECK(f.qr_title && f.index_title);
  set("project1.banner", "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW");
  CHECK(!project_fit(g_fb, c, 0).banner);
  // 20 px wide glyphs: fits the page at 14 px, not the column beside the QR.
  set("project2.title", "WWWWWWWWWWWWWWWWW");
  f = project_fit(g_fb, c, 1);
  CHECK(!f.qr_title);
}
