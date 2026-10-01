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

}  // namespace badge
