// Badger 2040 only: the UC8151 RAM image the panel backend sends.
#include "check.hpp"
#include "framebuffer.hpp"
#include "uc8151_pack.hpp"

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

TEST(uc8151_pack_matches_driver_pixel_layout) {
  static Framebuffer fb;
  static uint8_t ref[uc8151::kBytes], got[uc8151::kBytes];
  CHECK_EQ(uc8151::kBytes, 4736u);
  std::memset(ref, 0, sizeof ref);
  fb.clear(Ink::White);
  uint32_t seed = 1;
  for (int i = 0; i < 5000; ++i) {
    seed = seed * 1103515245u + 12345u;
    const int x = int(seed >> 8) % 296, y = int(seed >> 20) % 128, v = (seed >> 3) & 1;
    fb.set(x, y, v ? Ink::Black : Ink::White);
    ref_pixel(ref, x, y, v);
  }
  uc8151::pack(fb, got);
  CHECK(std::memcmp(got, ref, sizeof ref) == 0);
  fb.clear(Ink::Black);
  uc8151::pack(fb, got);
  for (uint8_t b : got) CHECK_EQ(b, 0xFF);
}

// The window the refresh policy sizes and the driver refreshes: whole 8-row
// banks, as Framebuffer::diff_bounds() returned before it became pixel-exact.
TEST(uc8151_partial_window_is_bank_aligned) {
  static Framebuffer a, b;
  a.clear(Ink::White);
  b.clear(Ink::White);
  b.set(100, 13, Ink::Black);
  b.set(140, 30, Ink::Black);
  const Rect d = uc8151::partial_window(a.diff_bounds(b));
  CHECK_EQ(d.x, 100);
  CHECK_EQ(d.w, 41);
  CHECK_EQ(d.y, 8);
  CHECK_EQ(d.h, 24);  // rows 8..31
  const Rect e = uc8151::partial_window({0, 120, 296, 8});
  CHECK_EQ(e.y, 120);
  CHECK_EQ(e.h, 8);
  const Rect f = uc8151::partial_window({5, 127, 1, 1});
  CHECK_EQ(f.y, 120);
  CHECK_EQ(f.h, 8);
  CHECK(uc8151::partial_window({}).empty());
}
