// Badger 2350 only: the SSD1680 RAM image the panel backend sends.
#include <initializer_list>
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
  ssd1680::pack(fb, got, ssd1680::Plane::Red);
  CHECK(std::memcmp(got, ref, sizeof ref) == 0);
}

TEST(ssd1680_four_tone_exact_vectors) {
  static Framebuffer fb;
  static uint8_t red[ssd1680::kPlaneBytes], bw[ssd1680::kPlaneBytes];
  fb.clear(Ink::White);
  const Ink inks[] = {Ink::White, Ink::LightGray, Ink::DarkGray, Ink::Black,
                     Ink::Black, Ink::DarkGray, Ink::LightGray, Ink::White};
  for (int y = 0; y < 8; ++y) fb.set(0, y, inks[y]);
  fb.set(263, 175, Ink::DarkGray);
  ssd1680::pack(fb, red, ssd1680::Plane::Red);
  ssd1680::pack(fb, bw, ssd1680::Plane::Bw);
  CHECK_EQ(red[0], 0x3c); CHECK_EQ(bw[0], 0x5a);
  CHECK_EQ(red[sizeof red - 1], 1); CHECK_EQ(bw[sizeof bw - 1], 0);
  for (size_t i = 1; i < sizeof red - 1; ++i) { CHECK_EQ(red[i], 0); CHECK_EQ(bw[i], 0); }
}

TEST(ssd1680_four_tone_matches_pinned_rgb_reference) {
  static Framebuffer fb;
  static uint8_t expected[ssd1680::kPlaneBytes], got[ssd1680::kPlaneBytes];
  constexpr Ink tones[] = {Ink::Black, Ink::DarkGray, Ink::LightGray, Ink::White};
  // Independent upstream RGB conversion: neutral RGB v=0,85,170,255.
  for (int y = 0; y < 176; ++y)
    for (int x = 0; x < 264; ++x) fb.set(x, y, tones[(x + 3 * y) & 3]);
  for (int bit : {7, 6}) {
    std::memset(expected, 0, sizeof expected);
    for (int y = 0; y < 176; ++y)
      for (int x = 0; x < 264; ++x) {
        const uint8_t luminance = uint8_t(((x + 3 * y) & 3) * 85);
        const uint8_t src = uint8_t(~(luminance >> bit)) & 1;
        expected[(y + x * 176) / 8] |= uint8_t(src << (7 - (y & 7)));
      }
    ssd1680::pack(fb, got, bit == 7 ? ssd1680::Plane::Red : ssd1680::Plane::Bw);
    CHECK(std::memcmp(got, expected, sizeof got) == 0);
  }
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
