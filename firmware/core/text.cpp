#include "text.hpp"

#include "crc32.hpp"

namespace badge {

namespace {
constexpr uint32_t kEllipsis = 0x2026;
constexpr char kEllipsisUtf8[] = "\xE2\x80\xA6";
constexpr size_t kEllipsisLen = 3;
}  // namespace

const Glyph *find_glyph(const Font &f, uint32_t cp) {
  int lo = 0, hi = int(f.glyph_count) - 1;
  while (lo <= hi) {
    const int mid = (lo + hi) / 2;
    const uint32_t c = f.glyphs[mid].codepoint;
    if (c == cp) return &f.glyphs[mid];
    if (c < cp) lo = mid + 1; else hi = mid - 1;
  }
  return nullptr;
}

uint32_t font_compute_crc(const Font &f) {
  uint32_t crc = 0;
  for (uint16_t i = 0; i < f.glyph_count; ++i) {
    const Glyph &g = f.glyphs[i];
    const uint8_t rec[12] = {uint8_t(g.codepoint), uint8_t(g.codepoint >> 8), g.w, g.h,
                             uint8_t(g.x_off), uint8_t(g.y_off), g.advance, 0,
                             uint8_t(g.offset), uint8_t(g.offset >> 8), uint8_t(g.offset >> 16),
                             uint8_t(g.offset >> 24)};
    crc = crc32_update(crc, rec, sizeof rec);
  }
  return crc32_update(crc, f.bitmap, f.bitmap_size);
}

size_t utf8_decode(const char *s, size_t len, uint32_t *cp) {
  const uint8_t *p = reinterpret_cast<const uint8_t *>(s);
  if (len == 0) { *cp = 0; return 0; }
  const uint8_t b0 = p[0];
  if (b0 < 0x80) { *cp = b0; return 1; }
  size_t n;
  uint32_t v, min;
  if ((b0 & 0xE0) == 0xC0) { n = 2; v = b0 & 0x1F; min = 0x80; }
  else if ((b0 & 0xF0) == 0xE0) { n = 3; v = b0 & 0x0F; min = 0x800; }
  else if ((b0 & 0xF8) == 0xF0) { n = 4; v = b0 & 0x07; min = 0x10000; }
  else { *cp = 0xFFFD; return 1; }
  if (len < n) { *cp = 0xFFFD; return 1; }
  for (size_t i = 1; i < n; ++i) {
    if ((p[i] & 0xC0) != 0x80) { *cp = 0xFFFD; return 1; }
    v = (v << 6) | (p[i] & 0x3F);
  }
  if (v < min || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) { *cp = 0xFFFD; return 1; }
  *cp = v;
  return n;
}

bool utf8_valid_printable(const char *s, size_t len) {
  size_t i = 0;
  while (i < len) {
    uint32_t cp;
    const size_t n = utf8_decode(s + i, len - i, &cp);
    if (cp == 0xFFFD && n == 1 && uint8_t(s[i]) >= 0x80) return false;
    if (cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp < 0xA0)) {
      if (cp != '\n') return false;  // hard newline is allowed for multi-line text
    }
    i += n;
  }
  return true;
}

size_t cstr_len(const char *s, size_t max) {
  size_t n = 0;
  while (n < max && s[n]) ++n;
  return n;
}

static const Glyph *glyph_or_fallback(const Font &f, uint32_t cp) {
  const Glyph *g = find_glyph(f, cp);
  if (!g) g = find_glyph(f, '?');
  return g;
}

int text_width(const Font &f, const char *s, size_t len) {
  int w = 0;
  size_t i = 0;
  while (i < len) {
    uint32_t cp;
    const size_t n = utf8_decode(s + i, len - i, &cp);
    if (cp == '\n') break;
    if (const Glyph *g = glyph_or_fallback(f, cp)) w += g->advance;
    i += n;
  }
  return w;
}

int text_width(const Font &f, const char *s) { return text_width(f, s, cstr_len(s, 4096)); }

int draw_text(Framebuffer &fb, const Font &f, int x, int top, const char *s, size_t len, Ink ink) {
  size_t i = 0;
  while (i < len) {
    uint32_t cp;
    const size_t n = utf8_decode(s + i, len - i, &cp);
    if (cp == '\n') break;
    if (const Glyph *g = glyph_or_fallback(f, cp)) {
      if (g->w && g->h) {
        fb.blit_mono(f.bitmap + g->offset, g->w, g->h, (g->w + 7) / 8, x + g->x_off, top + g->y_off,
                     true, ink);
      }
      x += g->advance;
    }
    i += n;
  }
  return x;
}

int draw_text(Framebuffer &fb, const Font &f, int x, int top, const char *s, Ink ink) {
  return draw_text(fb, f, x, top, s, cstr_len(s, 4096), ink);
}

// Largest byte prefix of s (at a character boundary) whose width plus
// `reserve` fits max_w.
static size_t prefix_fitting(const Font &f, const char *s, size_t len, int max_w, int reserve,
                             int *out_w) {
  int w = 0;
  size_t i = 0, fit = 0;
  while (i < len) {
    uint32_t cp;
    const size_t n = utf8_decode(s + i, len - i, &cp);
    if (cp == '\n') break;
    const Glyph *g = glyph_or_fallback(f, cp);
    const int adv = g ? g->advance : 0;
    if (w + adv + reserve > max_w) break;
    w += adv;
    i += n;
    fit = i;
  }
  if (out_w) *out_w = w;
  return fit;
}

FitResult fit_text(const Font *const *chain, int n, const char *s, int max_w) {
  FitResult r;
  const size_t len = cstr_len(s, 4096);
  for (int i = 0; i < n; ++i) {
    const int w = text_width(*chain[i], s, len);
    if (w <= max_w) {
      r.font = chain[i];
      r.bytes = len;
      r.width = w;
      return r;
    }
  }
  if (n == 0) return r;
  const Font &f = *chain[n - 1];
  const Glyph *e = find_glyph(f, kEllipsis);
  const int ew = e ? e->advance : 0;
  int w = 0;
  size_t fit = prefix_fitting(f, s, len, max_w, ew, &w);
  // Do not leave a dangling space before the ellipsis.
  while (fit > 0 && s[fit - 1] == ' ') { --fit; w = text_width(f, s, fit); }
  r.font = &f;
  r.bytes = fit;
  r.ellipsized = true;
  r.width = w + ew;
  return r;
}

void draw_fitted(Framebuffer &fb, const FitResult &r, const char *s, int x, int top, int max_w,
                 Align a, Ink ink) {
  if (!r.font) return;
  int dx = x;
  if (a == Align::Center) dx = x + (max_w - r.width) / 2;
  else if (a == Align::Right) dx = x + max_w - r.width;
  int pen = draw_text(fb, *r.font, dx, top, s, r.bytes, ink);
  if (r.ellipsized) draw_text(fb, *r.font, pen, top, kEllipsisUtf8, kEllipsisLen, ink);
}

int wrap_text(const Font &f, const char *s, int max_w, WrapLine *lines, int max_lines) {
  const size_t len = cstr_len(s, 4096);
  const Glyph *e = find_glyph(f, kEllipsis);
  const int ew = e ? e->advance : 0;
  int count = 0;
  size_t pos = 0;
  while (pos < len && count < max_lines) {
    while (pos < len && s[pos] == ' ') ++pos;  // skip leading spaces
    if (pos >= len) break;
    // Find the longest run of whole words that fits.
    size_t i = pos, last_break = pos;
    int w = 0;
    bool hard_nl = false;
    while (i < len) {
      uint32_t cp;
      const size_t n = utf8_decode(s + i, len - i, &cp);
      if (cp == '\n') { last_break = i; hard_nl = true; break; }
      const Glyph *g = glyph_or_fallback(f, cp);
      const int adv = g ? g->advance : 0;
      if (w + adv > max_w) break;
      w += adv;
      i += n;
      if (cp == ' ' || i == len) last_break = (cp == ' ') ? i - 1 : i;
    }
    size_t end;
    if (hard_nl || i >= len) end = hard_nl ? last_break : len;
    else if (last_break > pos) end = last_break;  // break at a space
    else end = i;                                 // single overlong word: hard break
    if (end == pos && !hard_nl) {                 // glyph wider than the line: force progress
      uint32_t cp;
      end = pos + utf8_decode(s + pos, len - pos, &cp);
    }
    // Trim trailing spaces.
    size_t e_end = end;
    while (e_end > pos && s[e_end - 1] == ' ') --e_end;
    WrapLine &L = lines[count++];
    L.start = pos;
    L.len = e_end - pos;
    L.ellipsized = false;
    pos = end;
    if (hard_nl) ++pos;  // consume the newline
    // Is there remaining visible text?
    size_t rest = pos;
    while (rest < len && (s[rest] == ' ' || s[rest] == '\n')) ++rest;
    if (count == max_lines && rest < len) {
      int fw = 0;
      size_t fit = prefix_fitting(f, s + L.start, L.len, max_w, ew, &fw);
      while (fit > 0 && s[L.start + fit - 1] == ' ') --fit;
      L.len = fit;
      L.ellipsized = true;
    }
  }
  return count;
}

void draw_wrapped_line(Framebuffer &fb, const Font &f, const char *s, const WrapLine &l, int x,
                       int top, Ink ink) {
  int pen = draw_text(fb, f, x, top, s + l.start, l.len, ink);
  if (l.ellipsized) draw_text(fb, f, pen, top, kEllipsisUtf8, kEllipsisLen, ink);
}

}  // namespace badge
