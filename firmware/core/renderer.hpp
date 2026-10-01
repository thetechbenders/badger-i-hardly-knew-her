// Screen renderer. A pure function of (View, content, assets, system info):
// the same code renders on the device and in the host preview tool.
#pragma once

#include <cstdint>

#include "app.hpp"
#include "assetpack.hpp"
#include "battery.hpp"
#include "framebuffer.hpp"
#include "qr.hpp"
#include "settings.hpp"

namespace badge {

struct InfoLines {
  static constexpr int kMax = 9;
  char line[kMax][56];
  int count = 0;
  void add(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
};

enum class GestureIndicator : uint8_t { Off, On, Fault };

// Top-right status area shown on every screen: gesture indicator, then the
// power state (USB, or battery bars with LOW / ? states). It reflects the
// last measurement at render time; e-paper keeps it while powered off.
struct StatusInfo {
  BatteryState battery;  // display == Unknown -> nothing drawn
  GestureIndicator gesture = GestureIndicator::Off;
};

struct RenderContext {
  const Settings *settings = nullptr;
  MonoBitmap portrait;           // may be invalid (missing asset)
  const InfoLines *info = nullptr;
  StatusInfo status;
  const char *recovery_reason = nullptr;
};

constexpr int kStatusWidth = 56;   // reserved width (right aligned)
constexpr int kStatusHeight = 8;   // rows 0..7 are never used by content

// Region reserved for the status area on this screen (for tests).
Rect status_rect(const View &v, const RenderContext &ctx);

// Layout geometry, exposed for tests and the preview tool.
struct CardGeometry {
  Rect qr;          // quiet-zone square (or placeholder box)
  QrStatus qr_status;
  int qr_scale;
  int qr_version;
};

void render(Framebuffer &fb, const View &v, const RenderContext &ctx);
// Describe where the card screen places its QR code (for tests).
CardGeometry card_geometry(const RenderContext &ctx, bool full_screen);
// Geometry of the n-th configured project's repository QR (Empty if it has no URL).
CardGeometry project_qr_geometry(const RenderContext &ctx, int project);

// Project index layout (for tests and the preview tool). Rows are
// kIndexRows tall slots of kIndexRowH pixels starting at list.y; `top` is the
// first configured project shown, `selected` the highlighted row's bar
// (empty when the list is empty). The scrollbar only appears when the list
// does not fit.
constexpr int kIndexRowH = 14;
struct IndexGeometry {
  int count = 0, top = 0, sel = 0, shown = 0;
  Rect list, selected, track, thumb;
  bool scrollbar = false;
};
IndexGeometry index_geometry(const View &v, const RenderContext &ctx);

// Compact label for a project link: "owner/repo" for github.com URLs, else
// the URL without "https://", "www." and a trailing '/'. The QR always
// encodes the full link; this is only what the page shows. Returns length.
size_t repo_label(const char *url, char *out, size_t cap);

// Whether each part of a project page was drawn completely (no ellipsis, no
// line dropped for lack of room), and the description's font size. Renders
// the page into `scratch` (for tests and the preview tool).
// banner, qr_title (the title beside the repository QR, linked projects only)
// and index_title (the project's row in the index) cover the other places
// the project's text appears.
struct ProjectFit {
  bool title = true, tagline = true, status = true, body = true, link = true;
  bool banner = true, qr_title = true, index_title = true;
  int body_px = 0;  // line height of the description font (0: no description)
};
ProjectFit project_fit(Framebuffer &scratch, const RenderContext &ctx, int project);

// The same check for the identity screens: whether each configured field
// was drawn completely on the badge (in the given layout), the card or the
// full-screen contact QR. Fields a screen does not show, and empty fields,
// count as complete. A contact line dropped for lack of room marks both its
// label and value. The card's QR caption is optional by design (contact
// lines win): left off for room it clears `caption_shown` only, and it is
// still shown in full on the full-screen QR.
struct ScreenFit {
  bool name = true, title = true, affiliation = true, interests = true, event = true, caption = true;
  bool caption_shown = true;
  bool contact_label[kMaxContacts], contact_value[kMaxContacts];
  ScreenFit() {
    for (int i = 0; i < kMaxContacts; ++i) contact_label[i] = contact_value[i] = true;
  }
};
// screen: Badge, Card or QrFull; layout only applies to the badge.
ScreenFit screen_fit(Framebuffer &scratch, const RenderContext &ctx, Screen screen, uint8_t layout);

}  // namespace badge
