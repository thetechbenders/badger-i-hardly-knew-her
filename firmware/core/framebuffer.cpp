#include "framebuffer.hpp"

#include <cstring>

#include "crc32.hpp"

namespace badge {

static_assert(Framebuffer::kBytes == 4736, "296x128 1bpp");

void Framebuffer::clear(Ink ink) { std::memset(buf_, ink == Ink::Black ? 0xFF : 0x00, kBytes); }

void Framebuffer::set(int x, int y, Ink ink) {
  if (x < clip_.x || y < clip_.y || x >= clip_.right() || y >= clip_.bottom()) return;
  uint8_t &b = buf_[size_t(x) * kColumnBytes + (y >> 3)];
  const uint8_t m = uint8_t(0x80u >> (y & 7));
  if (ink == Ink::Black) b |= m; else b &= uint8_t(~m);
}

Ink Framebuffer::get(int x, int y) const {
  if (x < 0 || y < 0 || x >= kWidth || y >= kHeight) return Ink::White;
  return (buf_[size_t(x) * kColumnBytes + (y >> 3)] & (0x80u >> (y & 7))) ? Ink::Black : Ink::White;
}

void Framebuffer::set_clip(Rect r) {
  int x0 = r.x < 0 ? 0 : r.x, y0 = r.y < 0 ? 0 : r.y;
  int x1 = r.right() > kWidth ? kWidth : r.right();
  int y1 = r.bottom() > kHeight ? kHeight : r.bottom();
  if (x1 < x0) x1 = x0;
  if (y1 < y0) y1 = y0;
  clip_ = {int16_t(x0), int16_t(y0), int16_t(x1 - x0), int16_t(y1 - y0)};
}

void Framebuffer::fill_rect(Rect r, Ink ink) {
  int x0 = r.x > clip_.x ? r.x : clip_.x;
  int y0 = r.y > clip_.y ? r.y : clip_.y;
  int x1 = r.right() < clip_.right() ? r.right() : clip_.right();
  int y1 = r.bottom() < clip_.bottom() ? r.bottom() : clip_.bottom();
  if (x0 >= x1 || y0 >= y1) return;
  // Build per-byte masks for the vertical span once, then apply per column.
  for (int x = x0; x < x1; ++x) {
    uint8_t *col = &buf_[size_t(x) * kColumnBytes];
    for (int y = y0; y < y1;) {
      const int bit = y & 7;
      const int n = (8 - bit) < (y1 - y) ? (8 - bit) : (y1 - y);
      const uint8_t m = uint8_t((0xFFu >> bit) & (0xFFu << (8 - bit - n)));
      if (ink == Ink::Black) col[y >> 3] |= m; else col[y >> 3] &= uint8_t(~m);
      y += n;
    }
  }
}

void Framebuffer::draw_rect(Rect r, Ink ink) {
  if (r.empty()) return;
  hline(r.x, r.y, r.w, ink);
  hline(r.x, r.bottom() - 1, r.w, ink);
  vline(r.x, r.y, r.h, ink);
  vline(r.right() - 1, r.y, r.h, ink);
}

void Framebuffer::blit_mono(const uint8_t *bits, int w, int h, int stride, int dx, int dy,
                            bool transparent, Ink fg) {
  const Ink bg = fg == Ink::Black ? Ink::White : Ink::Black;
  for (int y = 0; y < h; ++y) {
    const int py = dy + y;
    if (py < clip_.y || py >= clip_.bottom()) continue;
    const uint8_t *row = bits + size_t(y) * size_t(stride);
    for (int x = 0; x < w; ++x) {
      const bool on = row[x >> 3] & (0x80u >> (x & 7));
      if (on) set(dx + x, py, fg);
      else if (!transparent) set(dx + x, py, bg);
    }
  }
}

void Framebuffer::copy_from(const Framebuffer &o) {
  std::memcpy(buf_, o.buf_, kBytes);
  clip_ = o.clip_;
}

bool Framebuffer::equals(const Framebuffer &o) const { return std::memcmp(buf_, o.buf_, kBytes) == 0; }

uint32_t Framebuffer::hash() const { return crc32(buf_, kBytes); }

Rect Framebuffer::diff_bounds(const Framebuffer &o) const {
  int x0 = kWidth, x1 = -1, b0 = kColumnBytes, b1 = -1;
  for (int x = 0; x < kWidth; ++x) {
    const uint8_t *a = &buf_[size_t(x) * kColumnBytes];
    const uint8_t *b = &o.buf_[size_t(x) * kColumnBytes];
    for (int i = 0; i < kColumnBytes; ++i) {
      if (a[i] != b[i]) {
        if (x < x0) x0 = x;
        if (x > x1) x1 = x;
        if (i < b0) b0 = i;
        if (i > b1) b1 = i;
      }
    }
  }
  if (x1 < 0) return {};
  return {int16_t(x0), int16_t(b0 * 8), int16_t(x1 - x0 + 1), int16_t((b1 - b0 + 1) * 8)};
}

}  // namespace badge
