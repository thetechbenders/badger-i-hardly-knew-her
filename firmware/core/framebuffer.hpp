// Portable framebuffer: Classic 1 bpp, capable target 2 bpp, row-major/MSB first.
//
// Classic has MONO1 packing: stride ceil(width/8), 1 black / 0 white.
// Four-tone targets store logical Ink values in MSB-first 2-bit pairs:
// stride ceil(width/4), pixel x in bits 6-2*(x%4) of byte x/4. Logical
// Black remains 1; asset tone ordering is translated explicitly by blit_gray2.
// The size comes from the target's display_target.hpp (Badger 2040: 296 x
// 128, Badger 2350: 264 x 176, both landscape). Panel controllers want their
// own RAM layout; each panel backend packs from this buffer
// (firmware/platform/<target>/), so nothing here depends on a controller.
#pragma once

#include <cstddef>
#include <cstdint>

#include "display_target.hpp"

namespace badge {

struct Rect {
  int16_t x = 0, y = 0, w = 0, h = 0;
  constexpr int16_t right() const { return static_cast<int16_t>(x + w); }
  constexpr int16_t bottom() const { return static_cast<int16_t>(y + h); }
  constexpr bool empty() const { return w <= 0 || h <= 0; }
};

// Logical values independent of asset ordering and controller RAM bits.
// Ordinary UI drawing continues to use White/Black exclusively.
enum class Ink : uint8_t { White = 0, Black = 1, LightGray = 2, DarkGray = 3 };

class Framebuffer {
 public:
  static constexpr int kWidth = target::kDisplayWidth;
  static constexpr int kHeight = target::kDisplayHeight;
  static constexpr int kBpp = target::kFourTone ? 2 : 1;
  static constexpr int kPixelsPerByte = 8 / kBpp;
  static constexpr int kStride = (kWidth + kPixelsPerByte - 1) / kPixelsPerByte;
  static constexpr size_t kBytes = size_t(kStride) * kHeight;

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
  // Explicit GRAY2 asset blit; false on a target without four-tone support.
  bool blit_gray2(const uint8_t *bits, int w, int h, int stride, int dx, int dy);

  void copy_from(const Framebuffer &o);
  bool equals(const Framebuffer &o) const;
  uint32_t hash() const;  // CRC32 of the buffer, used for change suppression

  // Smallest rectangle covering all differing pixels (pixel exact). Empty if
  // equal. A panel widens it to its own partial-window granularity
  // (Panel::partial_window()).
  Rect diff_bounds(const Framebuffer &o) const;

  const uint8_t *data() const { return buf_; }

  void set_clip(Rect r);
  void reset_clip() { clip_ = {0, 0, kWidth, kHeight}; }
  Rect clip() const { return clip_; }

 private:
  uint8_t buf_[kBytes];
  Rect clip_{0, 0, kWidth, kHeight};
};

}  // namespace badge
