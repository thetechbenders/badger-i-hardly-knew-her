#include "gesture.hpp"

namespace badge {

const char *swipe_name(Swipe s) {
  switch (s) {
    case Swipe::Up: return "up";
    case Swipe::Right: return "right";
    case Swipe::Down: return "down";
    case Swipe::Left: return "left";
    default: return "none";
  }
}

void GestureDecoder::begin(uint32_t now_ms) {
  active_ = true;
  have_first_ = false;
  start_ms_ = now_ms;
  frames_ = valid_ = 0;
}

void GestureDecoder::add(const GestureFrame &f) {
  if (!active_) return;
  if (frames_ < 0xFFFF) ++frames_;
  const uint8_t nf = p_.noise_floor;
  if (f.u > nf && f.d > nf && f.l > nf && f.r > nf) {
    if (!have_first_) { first_ = f; have_first_ = true; }
    last_ = f;
    if (valid_ < 0xFFFF) ++valid_;
  }
}

static int ratio(int a, int b) { return (a + b) ? (100 * (a - b)) / (a + b) : 0; }

SessionResult GestureDecoder::finish() {
  SessionResult r;
  r.frames = frames_;
  r.valid_frames = valid_;
  active_ = false;
  if (have_first_) {
    r.first = first_;
    r.last = last_;
  }
  if (valid_ < 4) return r;  // too little data for a direction
  r.delta_ud = int16_t(ratio(last_.u, last_.d) - ratio(first_.u, first_.d));
  r.delta_lr = int16_t(ratio(last_.l, last_.r) - ratio(first_.l, first_.r));
  const int aud = r.delta_ud < 0 ? -r.delta_ud : r.delta_ud;
  const int alr = r.delta_lr < 0 ? -r.delta_lr : r.delta_lr;
  const int major = aud > alr ? aud : alr, minor = aud > alr ? alr : aud;
  if (major < p_.sensitivity) return r;
  if (major * 2 < minor * 3) return r;  // diagonal / ambiguous: need 1.5x dominance
  if (aud > alr) r.raw = r.delta_ud > 0 ? Swipe::Down : Swipe::Up;  // SparkFun convention
  else r.raw = r.delta_lr > 0 ? Swipe::Right : Swipe::Left;
  return r;
}

Swipe orient(Swipe raw, uint8_t rotation, bool mirror) {
  if (raw == Swipe::None) return raw;
  // Up=1, Right=2, Down=3, Left=4: clockwise order, so rotation is modular.
  int d = (int(raw) - 1 + (rotation & 3)) % 4;
  Swipe s = Swipe(d + 1);
  if (mirror) {
    if (s == Swipe::Left) s = Swipe::Right;
    else if (s == Swipe::Right) s = Swipe::Left;
  }
  return s;
}

bool SwipeGate::accept(Swipe s, uint32_t now_ms, uint16_t cooldown_ms) {
  if (s == Swipe::None) return false;
  if (have_ && now_ms - last_ms_ < cooldown_ms) {
    ++suppressed_;
    return false;
  }
  have_ = true;
  last_ms_ = now_ms;
  return true;
}

}  // namespace badge
