#include "renderer.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "icons.hpp"
#include "text.hpp"

namespace badge {

void InfoLines::add(const char *fmt, ...) {
  if (count >= kMax) return;
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(line[count], sizeof line[count], fmt, ap);
  va_end(ap);
  ++count;
}

namespace {

constexpr int W = Framebuffer::kWidth;
constexpr int H = Framebuffer::kHeight;

const Font *const kNameChain[] = {&fonts::sans_bold_24, &fonts::sans_bold_20, &fonts::sans_bold_17};
const Font *const kNameChainSmall[] = {&fonts::sans_bold_17, &fonts::sans_bold_14};
const Font *const kTitleChain[] = {&fonts::sans_bold_12, &fonts::sans_bold_10};
const Font *const kBodyChain[] = {&fonts::sans_11, &fonts::sans_10};
const Font *const kSmallChain[] = {&fonts::sans_10};

// QR symbol storage lives here (core 0 only renders): ~450 bytes.
QrSymbol g_qr;

struct Column {
  int x, w;
  int y;       // next free line top
  int bottom;  // exclusive
};

// Draw one fitted line into the column if there is room; returns false and
// draws nothing when the line would overflow the column bottom.
bool col_line(Framebuffer &fb, Column &c, const Font *const *chain, int n, const char *s, int gap_after,
              Align a = Align::Left, Ink ink = Ink::Black) {
  if (str_empty(s)) return false;
  FitResult r = fit_text(chain, n, s, c.w);
  if (c.y + r.font->line_height > c.bottom) return false;
  draw_fitted(fb, r, s, c.x, c.y, c.w, a, ink);
  c.y += r.font->line_height + gap_after;
  return true;
}

int col_wrapped(Framebuffer &fb, Column &c, const Font &f, const char *s, int max_lines, int gap_after,
                Ink ink = Ink::Black) {
  if (str_empty(s)) return 0;
  const int room = (c.bottom - c.y) / f.line_height;
  if (room <= 0) return 0;
  WrapLine lines[6];
  int limit = max_lines < room ? max_lines : room;
  if (limit > 6) limit = 6;
  const int n = wrap_text(f, s, c.w, lines, limit);
  for (int i = 0; i < n; ++i) {
    draw_wrapped_line(fb, f, s, lines[i], c.x, c.y, ink);
    c.y += f.line_height;
  }
  c.y += gap_after;
  return n;
}

// Title: one line of bold 12 px if it fits, else wrapped onto two lines of
// the same font (keeps long job titles whole instead of shrinking them).
void col_title(Framebuffer &fb, Column &c, const char *s, int gap_after) {
  if (str_empty(s)) return;
  const Font &f = fonts::sans_bold_12;
  if (text_width(f, s) <= c.w) {
    col_line(fb, c, kTitleChain, 1, s, gap_after);
    return;
  }
  if (c.y + 2 * f.line_height > c.bottom) {
    col_line(fb, c, kTitleChain, 2, s, gap_after);
    return;
  }
  col_wrapped(fb, c, f, s, 2, gap_after);
}

void draw_portrait(Framebuffer &fb, const MonoBitmap &p, int x, int y) {
  fb.blit_mono(p.bits, p.width, p.height, p.stride, x, y);
}

// ------------------------------------------------------------- status
// 16 x 7 battery: 15 x 7 body, 1 px terminal, four 2 px bars.
void draw_battery_icon(Framebuffer &fb, int x, int y, int bars, Ink ink) {
  fb.draw_rect({int16_t(x), int16_t(y), 15, 7}, ink);
  fb.fill_rect({int16_t(x + 15), int16_t(y + 2), 1, 3}, ink);
  for (int i = 0; i < bars && i < 4; ++i) fb.fill_rect({int16_t(x + 2 + 3 * i), int16_t(y + 2), 2, 3}, ink);
}

// 11 x 7 double-headed arrow ("swipe"); crossed out when the sensor is missing.
void draw_gesture_icon(Framebuffer &fb, int x, int y, bool fault, Ink ink) {
  fb.hline(x, y + 3, 11, ink);
  for (int i = 1; i <= 3; ++i) {  // thin chevron heads
    fb.set(x + i, y + 3 - i, ink);
    fb.set(x + i, y + 3 + i, ink);
    fb.set(x + 10 - i, y + 3 - i, ink);
    fb.set(x + 10 - i, y + 3 + i, ink);
  }
  if (fault)  // struck through: sensor missing or faulty
    for (int i = 0; i < 7; ++i) fb.set(x + 2 + i, y + 6 - i, ink);
}

// Right-aligned at xr (exclusive), rows y..y+6. Returns the left edge used.
int draw_status(Framebuffer &fb, const StatusInfo &st, int xr, int y, Ink ink) {
  const Font &f = fonts::sans_bold_10;
  const int text_top = y - (f.ascent - f.cap_height);  // caps occupy rows y..y+6
  int x = xr;
  switch (st.battery.display) {
    case PowerDisplay::Unknown: break;
    case PowerDisplay::Usb: {
      // USB power detected. Never "charging": the board has no charger.
      x -= text_width(f, "USB");
      draw_text(fb, f, x, text_top, "USB", ink);
      break;
    }
    case PowerDisplay::Battery:
    case PowerDisplay::Invalid: {
      x -= 16;
      const bool invalid = st.battery.display == PowerDisplay::Invalid;
      draw_battery_icon(fb, x, y, invalid ? 0 : st.battery.bars, ink);
      const char *tag = invalid ? "?" : (st.battery.low ? "LOW" : nullptr);
      if (tag) {
        x -= text_width(f, tag) + 2;
        draw_text(fb, f, x, text_top, tag, ink);
      }
      break;
    }
  }
  if (st.gesture != GestureIndicator::Off) {
    x -= (x == xr ? 11 : 11 + 4);
    draw_gesture_icon(fb, x, y, st.gesture == GestureIndicator::Fault, ink);
  }
  return x;
}

void draw_dashed_rect(Framebuffer &fb, Rect r) {
  for (int x = r.x; x < r.right(); ++x)
    if (((x - r.x) / 3) % 2 == 0) { fb.set(x, r.y, Ink::Black); fb.set(x, r.bottom() - 1, Ink::Black); }
  for (int y = r.y; y < r.bottom(); ++y)
    if (((y - r.y) / 3) % 2 == 0) { fb.set(r.x, y, Ink::Black); fb.set(r.right() - 1, y, Ink::Black); }
}

// Small solid triangle pointing up (dir = -1) or down (dir = +1).
void draw_triangle(Framebuffer &fb, int cx, int y, int dir) {
  for (int i = 0; i < 4; ++i) {
    const int row = dir < 0 ? y + i : y + 3 - i;
    fb.hline(cx - i, row, 2 * i + 1, Ink::Black);
  }
}

void draw_event_bar(Framebuffer &fb, int x, int w, const char *event) {
  // Black strip at the bottom of the text column with the event name in white.
  const int h = 16;
  fb.fill_rect({int16_t(x), int16_t(H - h), int16_t(w), int16_t(h)}, Ink::Black);
  const Font *chain[] = {&fonts::sans_bold_10};
  FitResult r = fit_text(chain, 1, event, w - 12);
  draw_fitted(fb, r, event, x + 6, H - h + 2, w - 12, Align::Left, Ink::White);
}

// ------------------------------------------------------------------ badge

void render_badge(Framebuffer &fb, const View &v, const RenderContext &ctx) {
  const Profile &p = ctx.settings->profile;
  const bool has_portrait = ctx.portrait.valid();
  const int pw = has_portrait ? ctx.portrait.width : 0;
  const bool right = v.layout == uint8_t(Layout::PortraitRight);
  const bool has_event = !str_empty(p.event);

  int px = 0, cx, cw;
  if (!has_portrait) { cx = 10; cw = W - 20; }
  else if (right) { px = W - pw; cx = 10; cw = px - 10 - 8; }
  else { px = 0; cx = pw + 10; cw = W - cx - 8; }
  if (has_portrait) draw_portrait(fb, ctx.portrait, px, (H - ctx.portrait.height) / 2);

  if (!right) {
    // Candidate A: portrait left, stacked identity, event strip at the bottom.
    const int bar_x = has_portrait ? pw : 0;
    const int bottom = has_event ? H - 16 - 4 : H - 6;
    Column c{cx, cw, 10, bottom};
    col_line(fb, c, kNameChain, 3, p.name, 2);
    col_title(fb, c, p.title, 1);
    col_line(fb, c, kBodyChain, 2, p.affiliation, 0);
    if (!str_empty(p.interests) && c.y + 6 + fonts::sans_10.line_height <= bottom) {
      c.y += 4;
      fb.hline(cx, c.y, 24, Ink::Black);
      fb.hline(cx, c.y + 1, 24, Ink::Black);
      c.y += 6;
      col_wrapped(fb, c, fonts::sans_10, p.interests, 3, 0);
    }
    if (has_event) draw_event_bar(fb, bar_x, W - bar_x, p.event);
  } else {
    // Candidate B: portrait right, event kicker on top, interests in a band.
    Column c{cx, cw, 7, H - 4};
    if (has_event) {
      FitResult r = fit_text(kSmallChain, 1, p.event, cw);
      fb.fill_rect({int16_t(cx), int16_t(c.y + 3), 3, 7}, Ink::Black);
      const Font *ev[] = {&fonts::sans_bold_10};
      r = fit_text(ev, 1, p.event, cw - 7);
      draw_fitted(fb, r, p.event, cx + 7, c.y, cw - 7);
      c.y += fonts::sans_bold_10.line_height + 3;
    } else {
      c.y += 4;
    }
    col_line(fb, c, kNameChain, 3, p.name, 1);
    col_title(fb, c, p.title, 1);
    col_line(fb, c, kBodyChain, 2, p.affiliation, 0);
    if (!str_empty(p.interests)) {
      // Interests band: white text on black, anchored to the bottom edge.
      WrapLine lines[3];
      const int n = wrap_text(fonts::sans_10, p.interests, cw + 10 - 12, lines, 2);
      const int band_h = n * fonts::sans_10.line_height + 6;
      const int band_y = H - band_h;
      if (band_y >= c.y + 2) {
        fb.fill_rect({0, int16_t(band_y), int16_t(cx + cw), int16_t(band_h)}, Ink::Black);
        for (int i = 0; i < n; ++i)
          draw_wrapped_line(fb, fonts::sans_10, p.interests, lines[i], cx, band_y + 3 + i * fonts::sans_10.line_height,
                            Ink::White);
      }
    }
  }
}

// ------------------------------------------------------------------- card

// Encode `payload` (which may be null = no QR) into g_qr and place it. Every
// QR screen calls this with its own payload on every render, so a code can
// never be left over from another screen or project.
CardGeometry qr_geometry_impl(const char *payload, bool full) {
  CardGeometry g{};
  const int max_px = H;  // the symbol + quiet zone may use the full height
  g.qr_status = qr_encode(payload ? payload : "", max_px, &g_qr);
  if (g.qr_status == QrStatus::Ok) {
    const int side = g_qr.px;
    const int x = full ? 0 : W - side;
    g.qr = {int16_t(x), int16_t((H - side) / 2), int16_t(side), int16_t(side)};
    g.qr_scale = g_qr.scale;
    g.qr_version = g_qr.version;
  } else if (g.qr_status == QrStatus::TooLong) {
    // Configuration error: keep a visible marker where the code would go.
    const int side = 108;
    g.qr = {int16_t(full ? 10 : W - side - 10), int16_t((H - side) / 2), int16_t(side), int16_t(side)};
  } else {
    g.qr = {int16_t(W), 0, 0, 0};  // not configured: nothing is drawn, text gets the width
  }
  return g;
}

CardGeometry card_geometry_impl(const RenderContext &ctx, bool full) {
  return qr_geometry_impl(ctx.settings->profile.qr_payload, full);
}

const Icon *contact_icon(const ContactLine &cl) {
  switch (contact_type(cl.type)) {
    case ContactType::GitHub: return &icons::github;
    case ContactType::Discord: return &icons::discord;
    default: return nullptr;
  }
}

void draw_icon(Framebuffer &fb, const Icon &ic, int x, int y) {
  fb.blit_mono(ic.bits, ic.w, ic.h, ic.stride, x, y, true);
}

// Only for a payload that cannot be encoded; an empty payload draws nothing.
void draw_qr_too_long(Framebuffer &fb, Rect r) {
  draw_dashed_rect(fb, r);
  const char *l1 = "QR PAYLOAD";
  const char *l2 = "TOO LONG";
  const char *l3 = "shorten qr.payload";
  const Font &b = fonts::sans_bold_12;
  const int y = r.y + (r.h - 2 * b.line_height - fonts::sans_10.line_height - 4) / 2;
  const Font *bc[] = {&b};
  const Font *sc[] = {&fonts::sans_10};
  FitResult f1 = fit_text(bc, 1, l1, r.w - 8), f2 = fit_text(bc, 1, l2, r.w - 8), f3 = fit_text(sc, 1, l3, r.w - 8);
  draw_fitted(fb, f1, l1, r.x + 4, y, r.w - 8, Align::Center);
  draw_fitted(fb, f2, l2, r.x + 4, y + b.line_height, r.w - 8, Align::Center);
  draw_fitted(fb, f3, l3, r.x + 4, y + 2 * b.line_height + 4, r.w - 8, Align::Center);
}

void render_card(Framebuffer &fb, const RenderContext &ctx) {
  const Profile &p = ctx.settings->profile;
  const CardGeometry g = card_geometry_impl(ctx, false);
  if (g.qr_status == QrStatus::Ok) qr_draw(fb, g_qr, g.qr.x, g.qr.y);
  else if (g.qr_status == QrStatus::TooLong) draw_qr_too_long(fb, g.qr);

  // A QR symbol carries its own white quiet zone; text may sit right next
  // to it. The error box gets a small gap; without a QR the text column
  // spans the card.
  const int left_w = g.qr_status == QrStatus::Empty ? W - 16
                                                    : g.qr.x - (g.qr_status == QrStatus::Ok ? 2 : 8) - 8;
  Column c{8, left_w, 9, H - 2};
  col_line(fb, c, kNameChainSmall, 2, p.name, 0);
  col_line(fb, c, kBodyChain, 2, p.title, 2);
  if (!str_empty(p.affiliation) && !str_empty(p.title)) {
    // Title and affiliation share the line budget: affiliation only if room.
    if (c.y + fonts::sans_10.line_height + 4 * 14 <= H) col_line(fb, c, kSmallChain, 1, p.affiliation, 2);
  } else {
    col_line(fb, c, kBodyChain, 2, p.affiliation, 2);
  }
  fb.hline(c.x, c.y + 1, 24, Ink::Black);
  fb.hline(c.x, c.y + 2, 24, Ink::Black);
  c.y += 6;

  // Contact lines: label column (bold text, or the platform icon for typed
  // GitHub/Discord lines instead of repeating the platform name) + value,
  // only for configured values. Values share one left edge.
  int label_w = 0, lines = 0;
  for (const auto &cl : p.contacts) {
    if (str_empty(cl.value)) continue;
    ++lines;
    const Icon *ic = contact_icon(cl);
    const int w = ic ? ic->w : (str_empty(cl.label) ? 0 : text_width(fonts::sans_bold_10, cl.label));
    if (w > label_w) label_w = w;
  }
  if (label_w > 52) label_w = 52;
  const int value_x = c.x + (label_w ? label_w + 6 : 0);
  const int value_w = c.x + c.w - value_x;
  const int pitch = 14;
  // The caption is optional: contact lines win when space runs out.
  const int caption_h = fonts::sans_10.line_height + 1;
  const bool has_caption = !str_empty(p.qr_caption) && g.qr_status == QrStatus::Ok &&
                           c.y + lines * pitch <= H - 2 - caption_h;
  const int bottom = has_caption ? H - 2 - caption_h : H - 1;
  for (const auto &cl : p.contacts) {
    if (str_empty(cl.value)) continue;
    if (c.y + pitch > bottom) break;
    if (const Icon *ic = contact_icon(cl)) {
      // 12 px icon centred on the value's cap height (caps span top+3..top+10).
      draw_icon(fb, *ic, c.x, c.y + 1);
    } else if (!str_empty(cl.label)) {
      const Font *lc[] = {&fonts::sans_bold_10};
      FitResult lr = fit_text(lc, 1, cl.label, label_w);
      draw_fitted(fb, lr, cl.label, c.x, c.y + 1, label_w);
    }
    FitResult vr = fit_text(kBodyChain, 2, cl.value, value_w);
    draw_fitted(fb, vr, cl.value, value_x, c.y + (vr.font == &fonts::sans_10 ? 1 : 0), value_w);
    c.y += pitch;
  }
  if (has_caption) {
    char buf[48];
    std::snprintf(buf, sizeof buf, "%s \xE2\x86\x92", p.qr_caption);  // caption + arrow
    FitResult r = fit_text(kSmallChain, 1, buf, c.w);
    draw_fitted(fb, r, buf, c.x, H - fonts::sans_10.line_height - 1, c.w);
  }
}

void render_qr_full(Framebuffer &fb, const RenderContext &ctx) {
  const Profile &p = ctx.settings->profile;
  const CardGeometry g = card_geometry_impl(ctx, true);
  if (g.qr_status == QrStatus::Ok) qr_draw(fb, g_qr, g.qr.x, g.qr.y);
  else draw_qr_too_long(fb, g.qr);
  const int x = g.qr.right() + 6;
  Column c{x, W - x - 8, 10, H - 6};
  col_line(fb, c, kNameChainSmall, 2, p.name, 4);
  col_wrapped(fb, c, fonts::sans_11, str_empty(p.qr_caption) ? "Scan with your phone camera" : p.qr_caption, 3, 6);
  // Show what the code opens so people can type it if scanning fails.
  const char *shown = p.qr_payload;
  if (std::strncmp(shown, "https://", 8) == 0) shown += 8;
  if (std::strncmp(p.qr_payload, "BEGIN:VCARD", 11) == 0) shown = "Offline contact card (vCard)";
  col_wrapped(fb, c, fonts::sans_10, shown, 3, 0);
}

// --------------------------------------------------------------- projects

// Tiny 9x9 QR-like glyph for the "hold B" hint (three finder squares).
void draw_qr_glyph(Framebuffer &fb, int x, int y) {
  auto finder = [&](int fx, int fy) {
    fb.draw_rect({int16_t(fx), int16_t(fy), 4, 4}, Ink::Black);
  };
  finder(x, y);
  finder(x + 5, y);
  finder(x, y + 5);
  fb.fill_rect({int16_t(x + 6), int16_t(y + 6), 2, 2}, Ink::Black);
}

const char *strip_scheme(const char *url) { return std::strncmp(url, "https://", 8) == 0 ? url + 8 : url; }

// Wrap a URL, preferring breaks after '/' (then after '-'), never inside a
// path segment unless the segment alone is wider than the column.
void col_url(Framebuffer &fb, Column &c, const Font &f, const char *url, int max_lines) {
  size_t pos = 0;
  const size_t len = cstr_len(url, 512);
  for (int line = 0; line < max_lines && pos < len; ++line) {
    if (c.y + f.line_height > c.bottom) return;
    const bool last = line == max_lines - 1;
    size_t best = 0, fit = 0;
    for (size_t i = pos + 1; i <= len; ++i) {
      if (text_width(f, url + pos, i - pos) > c.w) break;
      fit = i;
      if (i == len || url[i - 1] == '/' || url[i - 1] == '-') best = i;
    }
    size_t end = (fit == len) ? len : (best > pos ? best : fit);
    if (end <= pos) end = pos + 1;
    if (last && end < len) {  // out of lines: ellipsize the remainder
      const Font *fc[] = {&f};
      FitResult r = fit_text(fc, 1, url + pos, c.w);
      draw_fitted(fb, r, url + pos, c.x, c.y, c.w);
      c.y += f.line_height;
      return;
    }
    draw_text(fb, f, c.x, c.y, url + pos, end - pos);
    c.y += f.line_height;
    pos = end;
  }
}

// Description: 11 px while it fits, otherwise 10 px (one more line in the
// same space) before resorting to an ellipsis.
void col_body(Framebuffer &fb, Column &c, const char *s) {
  if (str_empty(s)) return;
  WrapLine probe[6];
  const int room11 = (c.bottom - c.y) / fonts::sans_11.line_height;
  const int n11 = room11 > 0 ? wrap_text(fonts::sans_11, s, c.w, probe, room11 < 6 ? room11 : 6) : 0;
  const bool fits11 = n11 > 0 && !probe[n11 - 1].ellipsized;
  col_wrapped(fb, c, fits11 ? fonts::sans_11 : fonts::sans_10, s, 6, 0);
}

void project_header(Framebuffer &fb, Column &c, int n, int total) {
  char hdr[24];
  std::snprintf(hdr, sizeof hdr, "PROJECT %d/%d", n + 1, total);
  const Font *hc[] = {&fonts::sans_bold_10};
  col_line(fb, c, hc, 1, hdr, 1);
}

void render_projects(Framebuffer &fb, const View &v, const RenderContext &ctx) {
  const Profile &p = ctx.settings->profile;
  const int total = configured_project_count(p);
  const int n = v.project < total ? v.project : 0;
  const int idx = nth_configured_project(p, n);
  const int right_margin = total > 1 ? 18 : 8;
  Column c{8, W - 8 - right_margin, 3, H - 2};
  if (idx < 0) return;  // unreachable: the app never shows an empty portfolio
  const Project &pr = p.projects[idx];
  project_header(fb, c, n, total);
  col_line(fb, c, kNameChainSmall, 2, pr.title, 1);

  if (!str_empty(pr.banner)) {
    // Prominent teaser banner: white on black across the column.
    const Font *bc[] = {&fonts::sans_bold_14, &fonts::sans_bold_12, &fonts::sans_bold_10};
    FitResult r = fit_text(bc, 3, pr.banner, c.w - 12);
    const int bh = r.font->line_height + 8;
    if (c.y + bh <= c.bottom) {
      fb.fill_rect({int16_t(c.x), int16_t(c.y + 2), int16_t(c.w), int16_t(bh)}, Ink::Black);
      draw_fitted(fb, r, pr.banner, c.x + 6, c.y + 6, c.w - 12, Align::Center, Ink::White);
      c.y += bh + 6;
    }
  }
  // Tagline: always bold 10 (same size on every page, so the hierarchy does
  // not shift while browsing), wrapping onto a second line if needed.
  col_wrapped(fb, c, fonts::sans_bold_10, pr.tagline, 2, 2);

  if (!str_empty(pr.status)) {
    // Outlined status tag, sized to its text.
    const Font *sc[] = {&fonts::sans_10};
    FitResult r = fit_text(sc, 1, pr.status, c.w - 8);
    const int th = fonts::sans_10.line_height + 1;
    if (c.y + th <= c.bottom) {
      fb.draw_rect({int16_t(c.x), int16_t(c.y), int16_t(r.width + 8), int16_t(th)}, Ink::Black);
      draw_fitted(fb, r, pr.status, c.x + 4, c.y, c.w - 8);
      c.y += th + 3;
    }
  }

  const char *url = project_url(p, n);
  const int footer_h = url ? fonts::sans_10.line_height + 1 : 0;
  Column body = c;
  body.bottom = H - 2 - footer_h;
  col_body(fb, body, pr.body);

  if (url) {
    // Footer: where the code points, and how to get it.
    const int fy = H - fonts::sans_10.line_height;
    const char *hint = "hold B";
    const int hint_w = 9 + 3 + text_width(fonts::sans_10, hint);
    const int hx = c.x + c.w - hint_w;
    draw_qr_glyph(fb, hx, fy + 2);
    draw_text(fb, fonts::sans_10, hx + 12, fy - 1, hint);
    const Font *uc[] = {&fonts::sans_10};
    const char *shown = strip_scheme(url);
    FitResult r = fit_text(uc, 1, shown, hx - c.x - 8);
    draw_fitted(fb, r, shown, c.x, fy - 1, hx - c.x - 8);
  }
  if (total > 1) {
    // Hints next to the UP/DOWN buttons on the right edge.
    draw_triangle(fb, W - 9, kStatusHeight + 8, -1);  // below the status area
    draw_triangle(fb, W - 9, H - 12, +1);
  }
}

CardGeometry project_qr_geometry_impl(const RenderContext &ctx, int n) {
  return qr_geometry_impl(project_url(ctx.settings->profile, n), true);
}

void render_project_qr(Framebuffer &fb, const View &v, const RenderContext &ctx) {
  const Profile &p = ctx.settings->profile;
  const int total = configured_project_count(p);
  const int n = v.project < total ? v.project : 0;
  const char *url = project_url(p, n);
  const CardGeometry g = project_qr_geometry_impl(ctx, n);
  if (!url || g.qr_status == QrStatus::Empty) {  // no URL: no code, show the page instead
    render_projects(fb, v, ctx);
    return;
  }
  if (g.qr_status == QrStatus::Ok) qr_draw(fb, g_qr, g.qr.x, g.qr.y);
  else draw_qr_too_long(fb, g.qr);
  const int x = g.qr.right() + 6;
  Column c{x, W - x - 8, 3, H - 2};
  project_header(fb, c, n, total);
  col_line(fb, c, kNameChainSmall, 2, p.projects[nth_configured_project(p, n)].title, 4);
  col_wrapped(fb, c, fonts::sans_11, "Scan to open the repository", 2, 4);
  Column u = c;
  u.bottom = H - fonts::sans_10.line_height - 3;
  col_url(fb, u, fonts::sans_10, strip_scheme(url), 3);
  const Font *hc[] = {&fonts::sans_10};
  const char *back = "hold B: back to project";
  FitResult r = fit_text(hc, 1, back, c.w);
  draw_fitted(fb, r, back, c.x, H - fonts::sans_10.line_height - 1, c.w);
}

// ----------------------------------------------------------- diagnostics

void render_info(Framebuffer &fb, const RenderContext &ctx, bool recovery) {
  int y = 10;  // rows 0..7 belong to the status area
  if (recovery) {
    fb.fill_rect({0, 0, W, 20}, Ink::Black);
    draw_text(fb, fonts::sans_bold_14, 6, 2, "SAFE MODE", Ink::White);
    const char *why = ctx.recovery_reason ? ctx.recovery_reason : "";
    const Font *rc[] = {&fonts::sans_10};
    const int rw = W - 100 - kStatusWidth - 8;
    FitResult r = fit_text(rc, 1, why, rw);
    draw_fitted(fb, r, why, 100, 8, rw, Align::Right, Ink::White);
    y = 24;
  }
  if (!ctx.info) return;
  for (int i = 0; i < ctx.info->count; ++i) {
    if (y + fonts::sans_10.line_height > H) break;
    FitResult r = fit_text(kSmallChain, 1, ctx.info->line[i], W - 12);
    draw_fitted(fb, r, ctx.info->line[i], 6, y, W - 12);
    y += fonts::sans_10.line_height;
  }
}

int status_right(const View &v, const RenderContext &ctx) {
  switch (v.screen) {
    case Screen::Badge:
      if (ctx.portrait.valid() && v.layout == uint8_t(Layout::PortraitRight)) return W - ctx.portrait.width - 4;
      return W - 2;
    case Screen::Card: {
      const CardGeometry g = card_geometry_impl(ctx, false);
      return g.qr_status == QrStatus::Empty ? W - 2 : g.qr.x - 4;
    }
    case Screen::Recovery: return W - 4;
    default: return W - 2;
  }
}

}  // namespace

CardGeometry card_geometry(const RenderContext &ctx, bool full_screen) { return card_geometry_impl(ctx, full_screen); }
CardGeometry project_qr_geometry(const RenderContext &ctx, int project) { return project_qr_geometry_impl(ctx, project); }

Rect status_rect(const View &v, const RenderContext &ctx) {
  const int xr = status_right(v, ctx);
  return {int16_t(xr - kStatusWidth), 0, int16_t(kStatusWidth), int16_t(kStatusHeight)};
}

void render(Framebuffer &fb, const View &v, const RenderContext &ctx) {
  fb.reset_clip();
  fb.clear(Ink::White);
  if (!ctx.settings) return;
  switch (v.screen) {
    case Screen::Badge: render_badge(fb, v, ctx); break;
    case Screen::Card: render_card(fb, ctx); break;
    case Screen::Projects: render_projects(fb, v, ctx); break;
    case Screen::QrFull:
      // Never reachable unconfigured (the app shows the card instead), but
      // keep the renderer total: no empty code screen.
      if (qr_configured(ctx.settings->profile)) render_qr_full(fb, ctx);
      else render_card(fb, ctx);
      break;
    case Screen::ProjectQr: render_project_qr(fb, v, ctx); break;
    case Screen::Info: render_info(fb, ctx, false); break;
    case Screen::Recovery: render_info(fb, ctx, true); break;
    default: break;
  }
  fb.reset_clip();
  const Rect sr = status_rect(v, ctx);
  fb.set_clip(sr);  // the status area can never spill into content
  draw_status(fb, ctx.status, sr.right(), 0, v.screen == Screen::Recovery ? Ink::White : Ink::Black);
  fb.reset_clip();
}

}  // namespace badge
