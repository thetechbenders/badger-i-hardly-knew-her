#include "renderer.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

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

void draw_portrait(Framebuffer &fb, const MonoBitmap &p, int x, int y) {
  fb.blit_mono(p.bits, p.width, p.height, p.stride, x, y);
}

void draw_battery_low(Framebuffer &fb, int x, int y) {
  // 13x7 outline battery with a single sliver of charge.
  fb.draw_rect({int16_t(x), int16_t(y), 12, 7}, Ink::Black);
  fb.fill_rect({int16_t(x + 12), int16_t(y + 2), 1, 3}, Ink::Black);
  fb.fill_rect({int16_t(x + 2), int16_t(y + 2), 2, 3}, Ink::Black);
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
    col_line(fb, c, kTitleChain, 2, p.title, 1);
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
    col_line(fb, c, kTitleChain, 2, p.title, 1);
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
  if (ctx.battery_low) draw_battery_low(fb, right || !has_portrait ? 2 : W - 15, 2);
}

// ------------------------------------------------------------------- card

CardGeometry card_geometry_impl(const RenderContext &ctx, bool full) {
  const Profile &p = ctx.settings->profile;
  CardGeometry g{};
  const int max_px = H;  // the symbol + quiet zone may use the full height
  g.qr_status = qr_encode(p.qr_payload, max_px, &g_qr);
  if (g.qr_status == QrStatus::Ok) {
    const int side = g_qr.px;
    const int x = full ? 0 : W - side;
    g.qr = {int16_t(x), int16_t((H - side) / 2), int16_t(side), int16_t(side)};
    g.qr_scale = g_qr.scale;
    g.qr_version = g_qr.version;
  } else {
    const int side = 108;
    g.qr = {int16_t(full ? 10 : W - side - 10), int16_t((H - side) / 2), int16_t(side), int16_t(side)};
  }
  return g;
}

void draw_qr_placeholder(Framebuffer &fb, Rect r, QrStatus st) {
  draw_dashed_rect(fb, r);
  const char *l1 = st == QrStatus::TooLong ? "QR PAYLOAD" : "QR NOT";
  const char *l2 = st == QrStatus::TooLong ? "TOO LONG" : "CONFIGURED";
  const char *l3 = st == QrStatus::TooLong ? "shorten qr.payload" : "set qr.payload";
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
  else draw_qr_placeholder(fb, g.qr, g.qr_status);

  // A QR symbol carries its own white quiet zone; text may sit right next
  // to it. The placeholder box gets a small gap.
  const int left_w = g.qr.x - (g.qr_status == QrStatus::Ok ? 2 : 8) - 8;
  Column c{8, left_w, 6, H - 4};
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

  // Contact lines: bold label column + value, only for configured values.
  int label_w = 0, lines = 0;
  for (const auto &cl : p.contacts) {
    if (str_empty(cl.value)) continue;
    ++lines;
    if (!str_empty(cl.label)) {
      const int w = text_width(fonts::sans_bold_10, cl.label);
      if (w > label_w) label_w = w;
    }
  }
  if (label_w > 52) label_w = 52;
  const int value_x = c.x + (label_w ? label_w + 6 : 0);
  const int value_w = c.x + c.w - value_x;
  const int pitch = 14;
  const bool has_caption = !str_empty(p.qr_caption) && g.qr_status == QrStatus::Ok;
  const int bottom = has_caption ? H - fonts::sans_10.line_height - 3 : H - 2;
  for (const auto &cl : p.contacts) {
    if (str_empty(cl.value)) continue;
    if (c.y + pitch > bottom) break;
    if (!str_empty(cl.label)) {
      const Font *lc[] = {&fonts::sans_bold_10};
      FitResult lr = fit_text(lc, 1, cl.label, label_w);
      draw_fitted(fb, lr, cl.label, c.x, c.y + 1, label_w);
    }
    FitResult vr = fit_text(kBodyChain, 2, cl.value, value_w);
    draw_fitted(fb, vr, cl.value, value_x, c.y + (vr.font == &fonts::sans_10 ? 1 : 0), value_w);
    c.y += pitch;
  }
  if (lines == 0) {
    const Font *sc[] = {&fonts::sans_10};
    const char *msg = "Contact details not configured";
    FitResult r = fit_text(sc, 1, msg, c.w);
    draw_fitted(fb, r, msg, c.x, c.y, c.w);
  }
  if (has_caption) {
    char buf[48];
    std::snprintf(buf, sizeof buf, "%s \xE2\x86\x92", p.qr_caption);  // caption + arrow
    FitResult r = fit_text(kSmallChain, 1, buf, c.w);
    draw_fitted(fb, r, buf, c.x, H - fonts::sans_10.line_height - 1, c.w);
  }
  if (ctx.battery_low) draw_battery_low(fb, 2, H - 9);
}

void render_qr_full(Framebuffer &fb, const RenderContext &ctx) {
  const Profile &p = ctx.settings->profile;
  const CardGeometry g = card_geometry_impl(ctx, true);
  if (g.qr_status == QrStatus::Ok) qr_draw(fb, g_qr, g.qr.x, g.qr.y);
  else draw_qr_placeholder(fb, g.qr, g.qr_status);
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

void render_projects(Framebuffer &fb, const View &v, const RenderContext &ctx) {
  const Profile &p = ctx.settings->profile;
  const int total = configured_project_count(p);
  const int idx = nth_configured_project(p, v.project < total ? v.project : 0);
  const int right_margin = total > 1 ? 18 : 8;
  Column c{8, W - 8 - right_margin, 5, H - 4};
  if (idx < 0) {
    col_line(fb, c, kTitleChain, 1, "No projects configured", 0);
    return;
  }
  const Project &pr = p.projects[idx];
  char hdr[24];
  if (total > 1) std::snprintf(hdr, sizeof hdr, "PROJECT %d/%d", v.project + 1, total);
  else std::snprintf(hdr, sizeof hdr, "PROJECT");
  const Font *hc[] = {&fonts::sans_bold_10};
  col_line(fb, c, hc, 1, hdr, 1);
  col_line(fb, c, kNameChainSmall, 2, pr.title, 0);
  col_line(fb, c, kTitleChain, 2, pr.tagline, 3);
  const bool has_link = !str_empty(pr.link);
  Column body = c;
  body.bottom = has_link ? H - fonts::sans_10.line_height - 3 : H - 3;
  col_wrapped(fb, body, fonts::sans_11, pr.body, 5, 0);
  if (has_link) {
    FitResult r = fit_text(kSmallChain, 1, pr.link, c.w);
    draw_fitted(fb, r, pr.link, c.x, H - fonts::sans_10.line_height - 1, c.w);
  }
  if (total > 1) {
    // Hints next to the UP/DOWN buttons on the right edge.
    draw_triangle(fb, W - 9, 8, -1);
    draw_triangle(fb, W - 9, H - 12, +1);
  }
}

// ----------------------------------------------------------- diagnostics

void render_info(Framebuffer &fb, const RenderContext &ctx, bool recovery) {
  int y = 2;
  if (recovery) {
    fb.fill_rect({0, 0, W, 20}, Ink::Black);
    draw_text(fb, fonts::sans_bold_14, 6, 2, "SAFE MODE", Ink::White);
    const char *why = ctx.recovery_reason ? ctx.recovery_reason : "";
    const Font *rc[] = {&fonts::sans_10};
    FitResult r = fit_text(rc, 1, why, W - 110);
    draw_fitted(fb, r, why, 100, 4, W - 106, Align::Right, Ink::White);
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

}  // namespace

CardGeometry card_geometry(const RenderContext &ctx, bool full_screen) { return card_geometry_impl(ctx, full_screen); }

void render(Framebuffer &fb, const View &v, const RenderContext &ctx) {
  fb.reset_clip();
  fb.clear(Ink::White);
  if (!ctx.settings) return;
  switch (v.screen) {
    case Screen::Badge: render_badge(fb, v, ctx); break;
    case Screen::Card: render_card(fb, ctx); break;
    case Screen::Projects: render_projects(fb, v, ctx); break;
    case Screen::QrFull: render_qr_full(fb, ctx); break;
    case Screen::Info: render_info(fb, ctx, false); break;
    case Screen::Recovery: render_info(fb, ctx, true); break;
    default: break;
  }
  fb.reset_clip();
}

}  // namespace badge
