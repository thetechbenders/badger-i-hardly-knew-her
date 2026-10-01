// 1-bit framebuffer in the UC8151 native memory layout used by the Badger 2040.
//
// Layout (identical to pimoroni::UC8151_Legacy::pixel()):
//   byte index = x * (HEIGHT / 8) + y / 8
//   bit        = 7 - (y % 8)          (MSB is the top pixel of the 8-pixel group)
//   bit value  = 1 -> black ink, 0 -> white
// The panel is 296 px wide (x) and 128 px tall (y) in landscape orientation.
#pragma once

#include <cstddef>
#include <cstdint>

namespace badge {

struct Rect {
  int16_t x = 0, y = 0, w = 0, h = 0;
  constexpr int16_t right() const { return static_cast<int16_t>(x + w); }
  constexpr int16_t bottom() const { return static_cast<int16_t>(y + h); }
  constexpr bool empty() const { return w <= 0 || h <= 0; }
};

enum class Ink : uint8_t { White = 0, Black = 1 };

class Framebuffer {
 public:
  static constexpr int kWidth = 296;
  static constexpr int kHeight = 128;
  static constexpr int kColumnBytes = kHeight / 8;               // 16
  static constexpr size_t kBytes = size_t(kWidth) * kColumnBytes;  // 4736

  Framebuffer() { clear(Ink::White); }

  void clear(Ink ink);
  void set(int x, int y, Ink ink);
  Ink get(int x, int y) const;
  void fill_rect(Rect r, Ink ink);
  void draw_rect(Rect r, Ink ink);  // 1 px outline
  void hline(int x, int y, int w, Ink ink) { fill_rect({int16_t(x), int16_t(y), int16_t(w), 1}, ink); }
  void vline(int x, int y, int h, Ink ink) { fill_rect({int16_t(x), int16_t(y), 1, int16_t(h)}, ink); }

  // Blit a row-major, MSB-first, 1 = black bitmap. Pixels outside the panel
  // or outside `clip` are discarded. When `transparent` is set, 0 bits leave
  // the destination untouched.
  void blit_mono(const uint8_t *bits, int w, int h, int stride, int dx, int dy,
                 bool transparent = false, Ink fg = Ink::Black);

  void copy_from(const Framebuffer &o);
  bool equals(const Framebuffer &o) const;
  uint32_t hash() const;  // CRC32 of the buffer, used for change suppression

  // Smallest rectangle covering all differing pixels, widened so y/h are
  // multiples of 8 (the UC8151 partial window granularity). Empty if equal.
  Rect diff_bounds(const Framebuffer &o) const;

  uint8_t *data() { return buf_; }
  const uint8_t *data() const { return buf_; }

  void set_clip(Rect r);
  void reset_clip() { clip_ = {0, 0, kWidth, kHeight}; }
  Rect clip() const { return clip_; }

 private:
  uint8_t buf_[kBytes];
  Rect clip_{0, 0, kWidth, kHeight};
};

}  // namespace badge
