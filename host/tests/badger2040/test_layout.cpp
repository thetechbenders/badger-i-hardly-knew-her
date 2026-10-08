// Badger 2040 only: limits of the 296 x 128 layouts.
#include "check.hpp"
#include "layout_helpers.hpp"

using namespace badge;
using namespace layout_test;

namespace {
Settings g_s;
Framebuffer g_fb;
}  // namespace

// Six lines under a two-line title and an affiliation: the last are dropped.
TEST(badger2040_card_drops_contact_lines_without_room) {
  crowded_card(g_s);
  RenderContext c = context(g_s);
  const ScreenFit f = screen_fit(g_fb, c, Screen::Card, 0);
  CHECK(f.contact_value[0]);
  CHECK(!f.contact_value[kMaxContacts - 1] && !f.contact_label[kMaxContacts - 1]);
}

// Five contact lines leave no room for the QR caption: contact lines win
// (by design), and the caption is not reported as cut.
TEST(badger2040_card_caption_gives_way_to_contacts) {
  captioned_card(g_s);
  RenderContext c = context(g_s);
  const ScreenFit f = screen_fit(g_fb, c, Screen::Card, 0);
  CHECK(!f.caption_shown);
  CHECK(f.caption);
}


// With the normal contact QR vertically clear of the top status strip, Card B
// keeps power/gesture status at the physical upper-right instead of shifting
// it left beside the QR.
TEST(badger2040_card_status_uses_free_top_right) {
  settings_defaults(&g_s);
  set(g_s, "qr.payload", "https://example.com/alex");
  RenderContext c = context(g_s);
  View v;
  v.screen = Screen::Card;
  const CardGeometry card = card_geometry(c, false);
  const Rect status = status_rect(v, c);

  CHECK(card.qr_status == QrStatus::Ok);
  CHECK(card.qr.y >= kStatusHeight);
  CHECK_EQ(status.right(), Framebuffer::kWidth - 2);
  CHECK(status.bottom() <= card.qr.y);
}
