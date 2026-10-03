// Badger 2350 only: the SSD1680 RAM image the panel backend sends.
#include "check.hpp"
#include "framebuffer.hpp"
#include "ssd1680_pack.hpp"

using namespace badge;

// Reference: the plane loop of pimoroni/badger2350's SSD1680::update() for a
// 264 x 176 frame buffer, with a black pixel giving 1 in both planes:
//   backbuffer[(y + x * HEIGHT) / 8] |= src << (7 - (y & 0b111));
static void ref_plane(const bool *black, uint8_t *out) {
  const int WIDTH = 264, HEIGHT = 176;
  std::memset(out, 0, (WIDTH * HEIGHT) / 8);
  for (int y = 0; y < HEIGHT; y++)
    for (int x = 0; x < WIDTH; x++) {
      const unsigned bo_d = 7 - (y & 0b111);
      const uint8_t src = black[x + y * WIDTH] ? 1 : 0;
      out[(y + x * HEIGHT) / 8] |= uint8_t(src << bo_d);
    }
}

TEST(ssd1680_pack_matches_reference_driver_loop) {
  static Framebuffer fb;
  static bool black[264 * 176];
  static uint8_t ref[ssd1680::kPlaneBytes], got[ssd1680::kPlaneBytes];
  CHECK_EQ(ssd1680::kPlaneBytes, 5808u);
  fb.clear(Ink::White);
  std::memset(black, 0, sizeof black);
  uint32_t seed = 3;
  for (int i = 0; i < 8000; ++i) {
    seed = seed * 1103515245u + 12345u;
    const int x = int(seed >> 8) % 264, y = int(seed >> 20) % 176;
    const bool v = (seed >> 3) & 1;
    fb.set(x, y, v ? Ink::Black : Ink::White);
    black[x + y * 264] = v;
  }
  ssd1680::pack(fb, got);
  ref_plane(black, ref);
  CHECK(std::memcmp(got, ref, sizeof ref) == 0);
}

TEST(ssd1680_pack_orientation) {
  static Framebuffer fb;
  static uint8_t got[ssd1680::kPlaneBytes];
  fb.clear(Ink::White);
  fb.set(0, 0, Ink::Black);      // top-left: first RAM row, first byte, MSB
  fb.set(263, 175, Ink::Black);  // bottom-right: last RAM row, last byte, LSB
  ssd1680::pack(fb, got);
  CHECK_EQ(got[0], 0x80);
  CHECK_EQ(got[sizeof got - 1], 0x01);
  int set = 0;
  for (uint8_t b : got) set += __builtin_popcount(b);
  CHECK_EQ(set, 2);
}
