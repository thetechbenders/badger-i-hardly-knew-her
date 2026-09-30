// Screen renderer. A pure function of (View, content, assets, system info):
// the same code renders on the device and in the host preview tool.
#pragma once

#include <cstdint>

#include "app.hpp"
#include "assetpack.hpp"
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

struct RenderContext {
  const Settings *settings = nullptr;
  MonoBitmap portrait;           // may be invalid (missing asset)
  const InfoLines *info = nullptr;
  bool battery_low = false;
  const char *recovery_reason = nullptr;
};

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

}  // namespace badge
