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
