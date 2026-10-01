#include "qr.hpp"

#include <cstring>

extern "C" {
#include "qrcodegen.h"
}

namespace badge {

namespace {
uint8_t g_temp[qrcodegen_BUFFER_LEN_FOR_VERSION(kQrMaxVersion)];
static_assert(sizeof(QrSymbol::data) >= qrcodegen_BUFFER_LEN_FOR_VERSION(kQrMaxVersion),
              "symbol buffer too small");

int max_version_for(int max_px) {
  int best = 0;
  for (int v = 1; v <= kQrMaxVersion; ++v)
    if ((4 * v + 17 + 2 * kQrQuietModules) * kQrMinScale <= max_px) best = v;
  return best;
}
}  // namespace

bool QrSymbol::module(int x, int y) const { return qrcodegen_getModule(data, x, y); }

QrStatus qr_encode(const char *payload, int max_px, QrSymbol *out) {
  out->status = QrStatus::Empty;
  if (!payload || !payload[0]) return out->status;
  const int vmax = max_version_for(max_px);
  out->status = QrStatus::TooLong;
  if (vmax < 1) return out->status;
  const qrcodegen_Ecc order[] = {qrcodegen_Ecc_MEDIUM, qrcodegen_Ecc_LOW};
  for (qrcodegen_Ecc ecl : order) {
    // qrcodegen picks the smallest version that fits and, with boostEcl,
    // raises the ECC level when that costs nothing.
    if (qrcodegen_encodeText(payload, g_temp, out->data, ecl, 1, vmax, qrcodegen_Mask_AUTO, true)) {
      out->modules = qrcodegen_getSize(out->data);
      out->version = (out->modules - 17) / 4;
      out->scale = max_px / (out->modules + 2 * kQrQuietModules);
      out->px = out->scale * (out->modules + 2 * kQrQuietModules);
      // Minimum level requested; boostEcl may have raised it further.
      out->ecc = ecl == qrcodegen_Ecc_MEDIUM ? 'M' : 'L';
      out->status = QrStatus::Ok;
      return out->status;
    }
  }
  return out->status;
}

void qr_draw(Framebuffer &fb, const QrSymbol &q, int x, int y) {
  if (q.status != QrStatus::Ok) return;
  fb.fill_rect({int16_t(x), int16_t(y), int16_t(q.px), int16_t(q.px)}, Ink::White);
  const int ox = x + kQrQuietModules * q.scale, oy = y + kQrQuietModules * q.scale;
  for (int my = 0; my < q.modules; ++my)
    for (int mx = 0; mx < q.modules; ++mx)
      if (q.module(mx, my))
        fb.fill_rect({int16_t(ox + mx * q.scale), int16_t(oy + my * q.scale), int16_t(q.scale),
                      int16_t(q.scale)},
                     Ink::Black);
}

}  // namespace badge
