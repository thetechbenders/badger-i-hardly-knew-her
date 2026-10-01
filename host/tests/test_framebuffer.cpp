#include "check.hpp"
#include "framebuffer.hpp"

using namespace badge;

// Reference: pimoroni::UC8151_Legacy::pixel() for a 296x128 panel.
static void ref_pixel(uint8_t *fb, int x, int y, int v) {
  const int height = 128;
  uint8_t *p = &fb[(y / 8) + (x * (height / 8))];
  uint8_t o = 7 - (y & 0b111);
  uint8_t m = ~(1 << o);
  uint8_t b = (v == 0 ? 0 : 1) << o;
  *p &= m;
  *p |= b;
}

TEST(framebuffer_layout_matches_uc8151_driver) {
  static Framebuffer fb;
  static uint8_t ref[Framebuffer::kBytes];
  std::memset(ref, 0, sizeof ref);
  fb.clear(Ink::White);
  uint32_t seed = 1;
  for (int i = 0; i < 5000; ++i) {
    seed = seed * 1103515245u + 12345u;
    const int x = int(seed >> 8) % 296, y = int(seed >> 20) % 128, v = (seed >> 3) & 1;
    fb.set(x, y, v ? Ink::Black : Ink::White);
    ref_pixel(ref, x, y, v);
  }
  CHECK(std::memcmp(fb.data(), ref, sizeof ref) == 0);
}

TEST(framebuffer_fill_rect_matches_set) {
  static Framebuffer a, b;
  const Rect rects[] = {{0, 0, 296, 128}, {3, 5, 17, 9}, {290, 120, 20, 20}, {-5, -5, 10, 10}, {10, 7, 1, 1}, {0, 8, 296, 8}};
  for (const Rect &r : rects) {
    a.clear(Ink::White);
    b.clear(Ink::White);
    a.fill_rect(r, Ink::Black);
    for (int x = r.x; x < r.right(); ++x)
      for (int y = r.y; y < r.bottom(); ++y) b.set(x, y, Ink::Black);
    CHECK(a.equals(b));
    a.fill_rect(r, Ink::White);
    CHECK_EQ(a.hash(), Framebuffer().hash());
  }
}

TEST(framebuffer_clip_and_bounds) {
  static Framebuffer fb;
  fb.clear(Ink::White);
  fb.set(-1, 0, Ink::Black);
  fb.set(296, 0, Ink::Black);
  fb.set(0, 128, Ink::Black);
  CHECK_EQ(fb.hash(), Framebuffer().hash());
  fb.set_clip({10, 10, 5, 5});
  fb.fill_rect({0, 0, 296, 128}, Ink::Black);
  fb.reset_clip();
  int black = 0;
  for (int x = 0; x < 296; ++x)
    for (int y = 0; y < 128; ++y) black += fb.get(x, y) == Ink::Black;
  CHECK_EQ(black, 25);
  CHECK(fb.get(10, 10) == Ink::Black && fb.get(15, 10) == Ink::White);
}

TEST(framebuffer_diff_bounds_is_partial_window_aligned) {
  static Framebuffer a, b;
  a.clear(Ink::White);
  b.clear(Ink::White);
  CHECK(a.diff_bounds(b).empty());
  b.set(100, 13, Ink::Black);
  b.set(140, 30, Ink::Black);
  Rect d = a.diff_bounds(b);
  CHECK_EQ(d.x, 100);
  CHECK_EQ(d.w, 41);
  CHECK_EQ(d.y, 8);
  CHECK_EQ(d.h, 24);  // rows 8..31
  CHECK_EQ(d.y % 8, 0);
  CHECK_EQ(d.h % 8, 0);
}

TEST(framebuffer_blit_mono_row_major) {
  static Framebuffer fb;
  fb.clear(Ink::White);
  const uint8_t bits[] = {0b10100000, 0b01000000};  // 3x2
  fb.blit_mono(bits, 3, 2, 1, 5, 6);
  CHECK(fb.get(5, 6) == Ink::Black);
  CHECK(fb.get(6, 6) == Ink::White);
  CHECK(fb.get(7, 6) == Ink::Black);
  CHECK(fb.get(6, 7) == Ink::Black);
  CHECK(fb.get(5, 7) == Ink::White);
}
