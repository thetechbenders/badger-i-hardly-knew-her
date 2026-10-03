// SSD1680 RAM image for the Badger 2350 panel (264 x 176, landscape).
//
// Portable (no SDK): used by panel_ssd1680.cpp and by the host tests.
//
// The controller's RAM X axis runs along the panel's 176-pixel side (22
// bytes per RAM row) and its Y axis along the 264-pixel side. With the data
// entry mode the backend sets (X increment, Y decrement from row 263), each
// landscape column x is one RAM row, written in order, so a byte holds 8
// vertically adjacent pixels:
//   byte index = x * (176 / 8) + y / 8
//   bit        = 7 - (y % 8)          (MSB is the top pixel of the group)
// This is the mapping of Pimoroni's reference driver (pimoroni/badger2350
// modules/c/ssd1680/ssd1680.cpp, MIT) for its 264 x 176 frame buffer.
//
// Tones: the panel shows four levels from two RAM planes (0x26 "red" and
// 0x24 "black/white") with the pinned reference waveform. For neutral RGB
// levels 255, 170, 85, 0 the reference takes complemented luminance bits
// 7 (red) and 6 (BW): white 00, light 01, dark 10, black 11 (red/BW).
#pragma once

#include <cstddef>
#include <cstdint>

#include "framebuffer.hpp"

namespace badge::ssd1680 {

constexpr int kRamRowBytes = Framebuffer::kHeight / 8;  // 22
constexpr size_t kPlaneBytes = size_t(Framebuffer::kWidth) * kRamRowBytes;  // 5808
static_assert(Framebuffer::kHeight % 8 == 0, "whole RAM bytes per landscape column");

enum class Plane : uint8_t { Red, Bw };

inline void pack(const Framebuffer &fb, uint8_t *out, Plane plane = Plane::Bw) {
  for (int x = 0; x < Framebuffer::kWidth; ++x) {
    uint8_t *row = out + size_t(x) * kRamRowBytes;
    for (int b = 0; b < kRamRowBytes; ++b) {
      uint8_t v = 0;
      for (int i = 0; i < 8; ++i) {
        const Ink ink = fb.get(x, b * 8 + i);
        if (ink == Ink::Black || (plane == Plane::Red ? ink == Ink::DarkGray : ink == Ink::LightGray))
          v |= uint8_t(0x80u >> i);
      }
      row[b] = v;
    }
  }
}

}  // namespace badge::ssd1680
