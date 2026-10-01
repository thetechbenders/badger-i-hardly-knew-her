// UTF-8 text measurement, drawing, fitting and wrapping on the 1-bit framebuffer.
#pragma once

#include <cstddef>
#include <cstdint>

#include "font.hpp"
#include "framebuffer.hpp"

namespace badge {

// Decode one UTF-8 scalar. Returns bytes consumed (>=1); invalid or
// overlong sequences decode to U+FFFD and consume one byte.
size_t utf8_decode(const char *s, size_t len, uint32_t *cp);
// True if `s` is valid, NUL-free UTF-8 without C0/C1 control characters.
bool utf8_valid_printable(const char *s, size_t len);
size_t cstr_len(const char *s, size_t max);

// Width in pixels of `len` bytes of `s`, including per-glyph advance.
int text_width(const Font &f, const char *s, size_t len);
int text_width(const Font &f, const char *s);

enum class Align : uint8_t { Left, Center, Right };

// Draw `len` bytes at (x, top); `top` is the line top (ascent line).
// Returns the pen x after the last glyph. Missing glyphs render as '?'.
int draw_text(Framebuffer &fb, const Font &f, int x, int top, const char *s, size_t len,
              Ink ink = Ink::Black);
int draw_text(Framebuffer &fb, const Font &f, int x, int top, const char *s, Ink ink = Ink::Black);

struct FitResult {
  const Font *font = nullptr;  // chosen font
  size_t bytes = 0;            // bytes of the source drawn before the ellipsis
  bool ellipsized = false;
  int width = 0;               // total drawn width including ellipsis
};

// Choose the first font in `chain` whose rendering of `s` fits `max_w`. If
// none fits, use the last font and truncate at a character boundary with "…".
FitResult fit_text(const Font *const *chain, int n, const char *s, int max_w);

// Draw a fit result inside [x, x+max_w) with alignment.
void draw_fitted(Framebuffer &fb, const FitResult &r, const char *s, int x, int top, int max_w,
                 Align a = Align::Left, Ink ink = Ink::Black);

struct WrapLine {
  size_t start = 0, len = 0;
  bool ellipsized = false;
};

// Greedy word-wrap into at most `max_lines`. Words longer than a line are
// broken at character boundaries. If text remains after the last line, that
// line is ellipsized. Returns the number of lines produced. Hard newlines
// ("\n") start new lines.
int wrap_text(const Font &f, const char *s, int max_w, WrapLine *lines, int max_lines);

// Draw a wrapped line (adds the ellipsis when flagged).
void draw_wrapped_line(Framebuffer &fb, const Font &f, const char *s, const WrapLine &l, int x,
                       int top, Ink ink = Ink::Black);

}  // namespace badge
