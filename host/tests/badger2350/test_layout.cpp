// Badger 2350 only: what the 264 x 176 layouts fit beyond the Badger 2040
// ones, and where they place things.
#include "check.hpp"
#include "layout_helpers.hpp"
#include "text.hpp"

using namespace badge;
using namespace layout_test;

namespace {
Settings g_s;
Framebuffer g_fb;
}  // namespace

TEST(badger2350_display_geometry) {
  CHECK_EQ(Framebuffer::kWidth, 264);
  CHECK_EQ(Framebuffer::kHeight, 176);
  CHECK_EQ(Framebuffer::kBytes, 11616u);  // 66 bytes per row
  CHECK_EQ(target::kPortraitWidth, 104);
  CHECK_EQ(target::kPortraitHeight, 176);
}

// The taller card keeps every contact line under a two-line title and an
// affiliation (the Badger 2040 drops the last ones).
TEST(badger2350_card_fits_all_contact_lines) {
  crowded_card(g_s);
  RenderContext c = context(g_s);
  const ScreenFit f = screen_fit(g_fb, c, Screen::Card, 0);
  for (int i = 0; i < kMaxContacts; ++i) CHECK(f.contact_value[i] && f.contact_label[i]);
  CHECK(f.name && f.title && f.affiliation);
}

// ... and the QR caption stays next to five contact lines.
TEST(badger2350_card_caption_beside_five_contacts) {
  captioned_card(g_s);
  RenderContext c = context(g_s);
  const ScreenFit f = screen_fit(g_fb, c, Screen::Card, 0);
  CHECK(f.caption_shown && f.caption);
}

// QR sizes: the card code keeps the Badger 2040 card's module size; the
// full-screen codes use larger modules (a short link, version 2: 25 + 8
// quiet modules at 4 px = 132 px, against 3 px on the card).
TEST(badger2350_qr_sizes) {
  settings_defaults(&g_s);
  set(g_s, "qr.payload", "https://example.com/alex");
  RenderContext c = context(g_s);
  const CardGeometry card = card_geometry(c, false), full = card_geometry(c, true);
  CHECK(card.qr_status == QrStatus::Ok && full.qr_status == QrStatus::Ok);
  CHECK_EQ(card.qr_version, 2);
  CHECK_EQ(card.qr_scale, 3);
  CHECK_EQ(full.qr_scale, 4);
  CHECK_EQ(full.qr.w, (25 + 2 * kQrQuietModules) * 4);
  CHECK(full.qr.x == 0 && full.qr.y == (Framebuffer::kHeight - full.qr.w) / 2);
}

// Beside the larger repository QR a long title wraps onto two lines instead
// of being cut, and a single long word falls back to bold 14.
TEST(badger2350_project_qr_title_wraps) {
  settings_defaults(&g_s);
  RenderContext c = context(g_s);
  const int n = configured_project_count(g_s.profile);
  for (int i = 0; i < n; ++i) CHECK(project_fit(g_fb, c, i).qr_title);
  set(g_s, "project2.title", "Shared Components");  // "Components" alone is too wide at bold 17
  CHECK(project_fit(g_fb, c, 1).qr_title);
}

// The index keeps seven rows (the app's scrolling is target-neutral) at a
// taller pitch, between the header rule and the hint line.
TEST(badger2350_index_rows_fit) {
  settings_defaults(&g_s);
  RenderContext c = context(g_s);
  View v;
  v.screen = Screen::Index;
  const IndexGeometry g = index_geometry(v, c);
  CHECK_EQ(kIndexRows, 7);
  CHECK_EQ(kIndexRowH, 20);
  CHECK(g.list.bottom() <= Framebuffer::kHeight - fonts::sans_10.line_height);
}
