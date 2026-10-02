// UC8151 RAM layout for the Badger 2040 panel (296 x 128).
//
// Portable (no SDK): used by panel_uc8151.cpp and by the host tests, which
// check it against pimoroni::UC8151_Legacy::pixel():
//   byte index = x * (128 / 8) + y / 8
//   bit        = 7 - (y % 8)          (MSB is the top pixel of the 8-pixel group)
//   bit value  = 1 -> black ink, 0 -> white
// Partial refresh windows cover whole 8-row banks (driver contract: y and h
// multiples of 8, x and w in pixels).
#pragma once

#include <cstddef>
#include <cstdint>

#include "framebuffer.hpp"

namespace badge::uc8151 {

constexpr int kColumnBytes = Framebuffer::kHeight / 8;  // 16
constexpr size_t kBytes = size_t(Framebuffer::kWidth) * kColumnBytes;
static_assert(Framebuffer::kHeight % 8 == 0, "whole 8-row banks");

inline void pack(const Framebuffer &fb, uint8_t *out) {
  for (int x = 0; x < Framebuffer::kWidth; ++x) {
    uint8_t *col = out + size_t(x) * kColumnBytes;
    for (int b = 0; b < kColumnBytes; ++b) {
      uint8_t v = 0;
      for (int i = 0; i < 8; ++i)
        if (fb.get(x, b * 8 + i) == Ink::Black) v |= uint8_t(0x80u >> i);
      col[b] = v;
    }
  }
}

// Widen a pixel-exact change to whole banks.
inline Rect partial_window(Rect d) {
  if (d.empty()) return d;
  const int y0 = d.y & ~7, y1 = (d.bottom() + 7) & ~7;
  return {d.x, int16_t(y0), d.w, int16_t(y1 - y0)};
}

}  // namespace badge::uc8151
