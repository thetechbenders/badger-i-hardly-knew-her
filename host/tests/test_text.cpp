#include <string>

#include "check.hpp"
#include "text.hpp"

using namespace badge;

TEST(font_tables_are_intact_and_sorted) {
  for (int i = 0; i < fonts::count; ++i) {
    const Font &f = *fonts::all[i];
    CHECK_EQ(font_compute_crc(f), f.crc);
    for (int g = 1; g < f.glyph_count; ++g) CHECK(f.glyphs[g - 1].codepoint < f.glyphs[g].codepoint);
    for (int g = 0; g < f.glyph_count; ++g) {
      const Glyph &gl = f.glyphs[g];
      CHECK(gl.offset + uint32_t((gl.w + 7) / 8) * gl.h <= f.bitmap_size);
      CHECK(gl.y_off >= -2 && gl.y_off + gl.h <= f.line_height + 2);
    }
    CHECK(find_glyph(f, 0x2026) != nullptr);  // ellipsis used for truncation
    CHECK(find_glyph(f, '?') != nullptr);
    CHECK(find_glyph(f, 0x00FC) != nullptr);  // u umlaut
  }
}

TEST(utf8_decode_and_validate) {
  uint32_t cp;
  CHECK_EQ(utf8_decode("A", 1, &cp), 1); CHECK_EQ(cp, 'A');
  CHECK_EQ(utf8_decode("\xC3\xBC", 2, &cp), 2); CHECK_EQ(cp, 0xFC);
  CHECK_EQ(utf8_decode("\xE2\x80\xA6", 3, &cp), 3); CHECK_EQ(cp, 0x2026);
  CHECK_EQ(utf8_decode("\xC0\x80", 2, &cp), 1); CHECK_EQ(cp, 0xFFFD);  // overlong
  CHECK_EQ(utf8_decode("\xE2\x80", 2, &cp), 1); CHECK_EQ(cp, 0xFFFD);  // truncated
  CHECK(utf8_valid_printable("M\xC3\xBCller", 7));
  CHECK(!utf8_valid_printable("bad\x01", 4));
  CHECK(!utf8_valid_printable("\xFF", 1));
  CHECK(!utf8_valid_printable("\xC2\x85", 2));  // C1 control
  CHECK(utf8_valid_printable("two\nlines", 9));
}

TEST(fit_text_picks_largest_font_that_fits) {
  const Font *chain[] = {&fonts::sans_bold_24, &fonts::sans_bold_20, &fonts::sans_bold_17};
  FitResult r = fit_text(chain, 3, "Alex Kim", 178);
  CHECK(r.font == &fonts::sans_bold_24);
  CHECK(!r.ellipsized);
  r = fit_text(chain, 3, "Maximilian Alexander Example", 178);
  CHECK(r.font != &fonts::sans_bold_24);
  CHECK(r.width <= 178);
}

TEST(fit_text_ellipsizes_within_width) {
  const Font *chain[] = {&fonts::sans_11};
  const char *s = "An extremely long contact value that cannot possibly fit on the badge";
  for (int w = 20; w < 200; w += 7) {
    FitResult r = fit_text(chain, 1, s, w);
    CHECK(r.ellipsized);
    CHECK(r.width <= w);
    CHECK(r.bytes < std::strlen(s));
    static Framebuffer fb;
    fb.clear(Ink::White);
    draw_fitted(fb, r, s, 0, 0, w);
    for (int x = w + 2; x < 296; ++x)  // glyph overhang tolerance of 1-2 px
      for (int y = 0; y < 20; ++y) CHECK(fb.get(x, y) == Ink::White);
  }
}

TEST(wrap_text_respects_width_and_line_limit) {
  const char *s = "3D printing \xC2\xB7 electronics \xC2\xB7 embedded systems \xC2\xB7 supercalifragilisticexpialidocious-words";
  WrapLine lines[4];
  const int n = wrap_text(fonts::sans_10, s, 90, lines, 3);
  CHECK(n == 3);
  for (int i = 0; i < n; ++i) CHECK(text_width(fonts::sans_10, s + lines[i].start, lines[i].len) <= 90);
  CHECK(lines[n - 1].ellipsized);
  // Hard newline splits lines.
  const char *t = "one\ntwo";
  CHECK_EQ(wrap_text(fonts::sans_10, t, 200, lines, 4), 2);
  CHECK_EQ(lines[1].start, 4u);
}

TEST(wrap_text_never_loops_on_adversarial_input) {
  uint32_t seed = 7;
  std::string s;
  for (int round = 0; round < 300; ++round) {
    s.clear();
    const int n = 1 + round % 90;
    for (int i = 0; i < n; ++i) {
      seed = seed * 1664525u + 1013904223u;
      const char pool[] = "W  i\n\xC3\xBC-";
      s.push_back(pool[(seed >> 16) % (sizeof pool - 1)]);
    }
    WrapLine lines[6];
    const int w = 1 + int(seed % 60);
    const int c = wrap_text(fonts::sans_bold_24, s.c_str(), w, lines, 6);
    CHECK(c >= 0 && c <= 6);
    for (int i = 0; i < c; ++i) CHECK(lines[i].start + lines[i].len <= s.size());
  }
}
