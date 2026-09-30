// Swipe recognition from APDS-9960 gesture FIFO data, mounting orientation,
// and debouncing. Pure logic: fed with FIFO frames, produces directions.
//
// Decoding (same principle as SparkFun's APDS-9960 library): within one
// gesture-engine session, take the first and last frames in which every
// photodiode is above the noise floor, compute the up/down and left/right
// balance ratios r = 100 * (a - b) / (a + b) for both, and compare. The axis
// with the larger change wins if the change exceeds the sensitivity and
// clearly dominates the other axis; otherwise the session is rejected.
// "Raw" directions use the SparkFun library's sensor-frame convention; the
// mounting rotation and mirror then map them to badge directions. Verify the
// mapping on the real mount (docs/GESTURE.md) and set gesture.rotation /
// gesture.mirror accordingly.
#pragma once

#include <cstdint>

namespace badge {

enum class Swipe : uint8_t { None = 0, Up, Right, Down, Left };
const char *swipe_name(Swipe s);

struct GestureFrame {
  uint8_t u, d, l, r;
};

struct GestureParams {
  uint8_t sensitivity = 30;   // minimum |delta ratio| in percent
  uint8_t noise_floor = 10;   // per-channel counts
  uint8_t rotation = 0;       // 0..3, x 90 degrees clockwise
  bool mirror = false;
  uint16_t cooldown_ms = 700;
  uint16_t max_session_ms = 1500;  // longer = hovering hand, not a swipe
};

struct SessionResult {
  Swipe raw = Swipe::None;
  int16_t delta_ud = 0, delta_lr = 0;
  uint16_t frames = 0, valid_frames = 0;
};

class GestureDecoder {
 public:
  void set_params(const GestureParams &p) { p_ = p; }
  void begin(uint32_t now_ms);
  void add(const GestureFrame &f);
  bool active() const { return active_; }
  uint32_t started_ms() const { return start_ms_; }
  // End the session and classify it (raw, sensor frame).
  SessionResult finish();

 private:
  GestureParams p_;
  bool active_ = false;
  bool have_first_ = false;
  uint32_t start_ms_ = 0;
  GestureFrame first_{}, last_{};
  uint16_t frames_ = 0, valid_ = 0;
};

// Rotate/mirror a sensor-frame direction into the badge frame.
Swipe orient(Swipe raw, uint8_t rotation, bool mirror);

// One swipe -> at most one event: suppresses everything within the cooldown
// after an accepted swipe (e.g. the hand's return stroke).
class SwipeGate {
 public:
  bool accept(Swipe s, uint32_t now_ms, uint16_t cooldown_ms);
  uint32_t suppressed() const { return suppressed_; }

 private:
  bool have_ = false;
  uint32_t last_ms_ = 0;
  uint32_t suppressed_ = 0;
};

}  // namespace badge
