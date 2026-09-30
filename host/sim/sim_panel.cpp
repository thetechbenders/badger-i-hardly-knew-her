#include "sim_panel.hpp"

namespace badge {

uint32_t SimPanel::expected_ms() const {
  static const uint32_t t[4] = {4500, 2000, 800, 250};  // UC8151_Legacy::update_time()
  return t[speed_ & 3];
}

bool SimPanel::busy() { return stuck || int32_t(busy_until_ - now()) > 0; }

void SimPanel::init(uint8_t speed) {
  stuck = false;
  speed_ = speed;
  busy_until_ = now();
  ops.push_back({'I', speed, {}});
}

void SimPanel::set_speed(uint8_t speed) {
  if (busy()) ++violations;
  speed_ = speed;
  ops.push_back({'S', speed, {}});
}

void SimPanel::start_full(const Framebuffer &fb) {
  if (busy()) ++violations;
  image.copy_from(fb);
  busy_until_ = now() + expected_ms();
  ops.push_back({'F', speed_, {0, 0, Framebuffer::kWidth, Framebuffer::kHeight}});
}

void SimPanel::start_partial(const Framebuffer &fb, Rect r) {
  if (busy()) ++violations;
  if (r.y % 8 || r.h % 8) ++violations;  // UC8151 partial window granularity
  for (int x = r.x; x < r.right(); ++x)
    for (int y = r.y; y < r.bottom(); ++y) image.set(x, y, fb.get(x, y));
  busy_until_ = now() + expected_ms();
  ops.push_back({'P', speed_, r});
}

void SimPanel::finish() {
  if (busy()) ++violations;
  ops.push_back({'O', speed_, {}});
}

}  // namespace badge
