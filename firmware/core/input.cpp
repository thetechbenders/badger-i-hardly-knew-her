#include "input.hpp"

namespace badge {

void ButtonTracker::reset(uint32_t held_at_boot_mask) {
  stable_ = 0;
  for (int i = 0; i < kButtonCount; ++i) {
    st_[i] = State{};
    if (held_at_boot_mask & (1u << i)) {
      st_[i].suppressed = true;
      st_[i].pressed = true;
      st_[i].integrator = kSteps;
      stable_ |= 1u << i;
    }
  }
}

int ButtonTracker::sample(uint32_t pressed_mask, uint32_t now_ms, ButtonEvent *out, int max) {
  int n = 0;
  for (int i = 0; i < kButtonCount; ++i) {
    State &s = st_[i];
    const bool raw = pressed_mask & (1u << i);
    if (raw && s.integrator < kSteps) ++s.integrator;
    else if (!raw && s.integrator > 0) --s.integrator;

    if (!s.pressed && s.integrator == kSteps) {
      s.pressed = true;
      s.long_fired = false;
      s.down_ms = now_ms;
      stable_ |= 1u << i;
    } else if (s.pressed && s.integrator == 0) {
      s.pressed = false;
      stable_ &= ~(1u << i);
      if (s.suppressed) {
        s.suppressed = false;
      } else if (!s.long_fired && n < max) {
        out[n++] = {Button(i), Gesture::Short, now_ms};
      }
    }
    if (s.pressed && !s.suppressed && !s.long_fired && now_ms - s.down_ms >= kLongPressMs) {
      s.long_fired = true;
      if (n < max) out[n++] = {Button(i), Gesture::Long, now_ms};
    }
  }
  return n;
}

const char *button_name(Button b) {
  switch (b) {
    case Button::A: return "A";
    case Button::B: return "B";
    case Button::C: return "C";
    case Button::Up: return "UP";
    case Button::Down: return "DOWN";
    case Button::User: return "USR";
    default: return "?";
  }
}

}  // namespace badge
