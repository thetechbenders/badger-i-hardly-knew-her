#include "admission.hpp"
#include "qr.hpp"
#include "text.hpp"
namespace badge {
bool render_admission(Framebuffer &fb, const Admission *a) {
  fb.reset_clip();
  fb.clear(Ink::White);
  QrSymbol q;
  // Labels occupy only the narrow right column; all 176 rows are available
  // to the QR, including its full quiet zone. No ordinary badge/status text.
  if (Framebuffer::kWidth != 264 || Framebuffer::kHeight != 176 || !a ||
      qr_encode(a->payload, 176, &q, true) != QrStatus::Ok ||
      text_width(fonts::sans_11, a->event) > 82 ||
      text_width(fonts::sans_11, a->holder) > 82) {
    draw_text(fb, fonts::sans_11, 8, 30, "Admission unavailable");
    return false;
  }
  qr_draw(fb, q, (176 - q.px) / 2, (176 - q.px) / 2);
  draw_text(fb, fonts::sans_11, 180, 65, a->event);
  draw_text(fb, fonts::sans_11, 180, 85, a->holder);
  return true;
}
}
