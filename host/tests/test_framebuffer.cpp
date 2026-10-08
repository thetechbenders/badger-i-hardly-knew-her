#include "check.hpp"
#include "framebuffer.hpp"

using namespace badge;

constexpr int W = Framebuffer::kWidth, H = Framebuffer::kHeight;

// The board-neutral layout: row-major, MSB first, 1 = black (as MONO1/PBM).
TEST(framebuffer_layout_is_row_major_msb_first) {
  static Framebuffer fb;
  static uint8_t ref[Framebuffer::kBytes];
  CHECK_EQ(Framebuffer::kStride, target::kFourTone ? (W + 3) / 4 : (W + 7) / 8);
  CHECK_EQ(Framebuffer::kBytes, size_t(Framebuffer::kStride) * H);
  std::memset(ref, 0, sizeof ref);
  fb.clear(Ink::White);
  uint32_t seed = 1;
  for (int i = 0; i < 5000; ++i) {
    seed = seed * 1103515245u + 12345u;
    const int x = int(seed >> 8) % W, y = int(seed >> 20) % H, v = (seed >> 3) & 1;
    fb.set(x, y, v ? Ink::Black : Ink::White);
    const int ppb = Framebuffer::kPixelsPerByte, bpp = Framebuffer::kBpp;
    uint8_t &b = ref[y * Framebuffer::kStride + x / ppb];
    const int shift = 8 - bpp * (x % ppb + 1);
    const uint8_t m = uint8_t(((1u << bpp) - 1) << shift);
    b = uint8_t((b & ~m) | (v << shift));
  }
  CHECK(std::memcmp(fb.data(), ref, sizeof ref) == 0);
}

TEST(framebuffer_fill_rect_matches_set) {
  static Framebuffer a, b;
  const Rect rects[] = {{0, 0, W, H}, {3, 5, 17, 9}, {W - 6, H - 8, 20, 20}, {-5, -5, 10, 10}, {10, 7, 1, 1}, {0, 8, W, 8},
                        {7, 0, 2, H}, {1, 3, 14, 1}};
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
  fb.set(W, 0, Ink::Black);
  fb.set(0, H, Ink::Black);
  CHECK_EQ(fb.hash(), Framebuffer().hash());
  fb.set_clip({10, 10, 5, 5});
  fb.fill_rect({0, 0, W, H}, Ink::Black);
  fb.reset_clip();
  int black = 0;
  for (int x = 0; x < W; ++x)
    for (int y = 0; y < H; ++y) black += fb.get(x, y) == Ink::Black;
  CHECK_EQ(black, 25);
  CHECK(fb.get(10, 10) == Ink::Black && fb.get(15, 10) == Ink::White);
}

TEST(framebuffer_diff_bounds_is_pixel_exact) {
  static Framebuffer a, b;
  a.clear(Ink::White);
  b.clear(Ink::White);
  CHECK(a.diff_bounds(b).empty());
  b.set(100, 13, Ink::Black);
  b.set(140, 30, Ink::Black);
  Rect d = a.diff_bounds(b);
  CHECK_EQ(d.x, 100);
  CHECK_EQ(d.w, 41);
  CHECK_EQ(d.y, 13);
  CHECK_EQ(d.h, 18);  // rows 13..30
  // Corners and single pixels inside one byte.
  a.clear(Ink::White);
  b.clear(Ink::White);
  b.set(W - 1, H - 1, Ink::Black);
  d = a.diff_bounds(b);
  CHECK(d.x == W - 1 && d.y == H - 1 && d.w == 1 && d.h == 1);
  b.set(0, 0, Ink::Black);
  d = a.diff_bounds(b);
  CHECK(d.x == 0 && d.y == 0 && d.w == W && d.h == H);
  b.clear(Ink::White);
  b.set(3, 5, Ink::Black);
  b.set(5, 5, Ink::Black);
  d = a.diff_bounds(b);
  CHECK(d.x == 3 && d.w == 3 && d.y == 5 && d.h == 1);
  // A random set of changes: the bounds are exactly their extent.
  uint32_t seed = 7;
  for (int round = 0; round < 50; ++round) {
    b.copy_from(a);
    int x0 = W, x1 = -1, y0 = H, y1 = -1;
    for (int i = 0; i < 1 + round % 5; ++i) {
      seed = seed * 1103515245u + 12345u;
      const int x = int(seed >> 8) % W, y = int(seed >> 20) % H;
      b.set(x, y, Ink::Black);
      x0 = x < x0 ? x : x0; x1 = x > x1 ? x : x1;
      y0 = y < y0 ? y : y0; y1 = y > y1 ? y : y1;
    }
    d = a.diff_bounds(b);
    CHECK(d.x == x0 && d.y == y0 && d.right() == x1 + 1 && d.bottom() == y1 + 1);
  }
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
