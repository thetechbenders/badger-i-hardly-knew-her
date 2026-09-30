// QR encoding (Nayuki qrcodegen) and pixel-exact placement on the panel.
//
// Rules: integer module size (>= kMinScale px), a 4-module white quiet zone
// drawn into the bitmap on every side, error correction M (boosted when it
// fits) falling back to L only when M cannot fit. No payload -> no symbol.
#pragma once

#include <cstdint>

#include "framebuffer.hpp"

namespace badge {

constexpr int kQrQuietModules = 4;
constexpr int kQrMinScale = 2;       // 2 px = 0.45 mm on the 2.9" panel
constexpr int kQrMaxVersion = 10;    // 57 modules; bounded buffers

enum class QrStatus : uint8_t { Ok, Empty, TooLong };

struct QrSymbol {
  QrStatus status = QrStatus::Empty;
  int version = 0;
  int modules = 0;  // modules per side (without quiet zone)
  int scale = 0;    // pixels per module
  int px = 0;       // total side in pixels including quiet zone
  char ecc = '-';   // minimum ECC level guaranteed (L or M)
  uint8_t data[(4 * kQrMaxVersion + 17) * (4 * kQrMaxVersion + 17) / 8 + 2];
  bool module(int x, int y) const;
};

// Encode `payload` to fit a square of at most `max_px` pixels (quiet zone
// included), choosing the largest integer scale that fits.
QrStatus qr_encode(const char *payload, int max_px, QrSymbol *out);
// Draw the symbol with its quiet zone; (x, y) is the top-left of the quiet zone.
void qr_draw(Framebuffer &fb, const QrSymbol &q, int x, int y);

}  // namespace badge
