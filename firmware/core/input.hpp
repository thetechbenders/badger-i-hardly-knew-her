// Button debouncing and gesture detection.
//
// Sampling: the platform samples the raw GPIO levels every kSampleMs from a
// timer interrupt and feeds them to ButtonTracker::sample(). A button counts
// as pressed after kDebounceMs of consistently "pressed" samples, and as
// released after kDebounceMs of consistently "released" samples.
//
// Gestures (per button):
//   Short press - emitted on release when the hold lasted < kLongPressMs.
//   Long press  - emitted once, while still held, when the hold reaches
//                 kLongPressMs. The following release emits nothing.
// Buttons already held at boot (the wake button on battery) are ignored
// until they have been released once, so waking never triggers a long press.
#pragma once

#include <cstdint>

namespace badge {

enum class Button : uint8_t { A = 0, B, C, Up, Down, User, Count };
constexpr int kButtonCount = int(Button::Count);

enum class Gesture : uint8_t { Short, Long };

struct ButtonEvent {
  Button button;
  Gesture gesture;
  uint32_t t_ms;
};

constexpr uint32_t kSampleMs = 5;
constexpr uint32_t kDebounceMs = 20;
constexpr uint32_t kLongPressMs = 1000;

class ButtonTracker {
 public:
  // `held_at_boot` bit i => Button i is ignored until its first release.
  void reset(uint32_t held_at_boot_mask);
  // `pressed_mask` bit i => Button i currently reads as pressed.
  // Writes up to `max` events; returns how many were produced.
  int sample(uint32_t pressed_mask, uint32_t now_ms, ButtonEvent *out, int max);
  uint32_t stable_mask() const { return stable_; }
  bool any_held() const { return stable_ != 0; }

 private:
  struct State {
    uint8_t integrator = 0;  // 0 .. kSteps
    bool pressed = false;
    bool long_fired = false;
    bool suppressed = false;
    uint32_t down_ms = 0;
  };
  static constexpr uint8_t kSteps = kDebounceMs / kSampleMs;
  State st_[kButtonCount];
  uint32_t stable_ = 0;
};

const char *button_name(Button b);

}  // namespace badge
