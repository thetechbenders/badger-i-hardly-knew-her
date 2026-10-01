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
  auto emit = [&](int i, Gesture g, uint8_t count) {
    if (n < max) out[n++] = {Button(i), g, now_ms, count};
  };
  for (int i = 0; i < kButtonCount; ++i) {
    State &s = st_[i];
    const bool raw = pressed_mask & (1u << i);
    if (raw && s.integrator < kSteps) ++s.integrator;
    else if (!raw && s.integrator > 0) --s.integrator;

    if (!s.pressed && s.integrator == kSteps) {
      s.pressed = true;
      s.long_fired = false;
      s.repeats = 0;
      s.down_ms = now_ms;
      s.next_repeat_ms = now_ms + kRepeatDelayMs;
      stable_ |= 1u << i;
      emit(i, Gesture::Press, 0);
    } else if (s.pressed && s.integrator == 0) {
      s.pressed = false;
      stable_ &= ~(1u << i);
      if (s.suppressed) {
        s.suppressed = false;
      } else if (!s.long_fired) {
        emit(i, Gesture::Short, s.repeats);
      }
    }
    if (!s.pressed || s.suppressed) continue;
    if ((kRepeatMask & (1u << i)) && int32_t(now_ms - s.next_repeat_ms) >= 0) {
      if (s.repeats < 255) ++s.repeats;
      s.next_repeat_ms += kRepeatIntervalMs;
      emit(i, Gesture::Repeat, s.repeats);
    }
    if (!s.long_fired && now_ms - s.down_ms >= kLongPressMs) {
      s.long_fired = true;
      emit(i, Gesture::Long, 0);
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

const char *click_name(Click c) {
  switch (c) {
    case Click::None: return "none";
    case Click::Single: return "single";
    case Click::Double: return "double";
    case Click::Long: return "long";
  }
  return "?";
}

Click ClickRecognizer::on_event(Gesture g, uint32_t t_ms, bool immediate) {
  switch (g) {
    case Gesture::Press:
      // A press inside the window starts the second half of a double press;
      // anything else starts a new interaction (resynchronises after a lost
      // release event).
      {
        const bool in_window = state_ == State::Waiting && int32_t(t_ms - release_ms_) < int32_t(kDoublePressMs);
        state_ = in_window ? State::SecondDown : State::FirstDown;
      }
      return Click::None;
    case Gesture::Short:
      if (state_ == State::SecondDown) {
        state_ = State::Idle;
        return Click::Double;
      }
      if (state_ != State::FirstDown) return Click::None;  // not ours (cancelled, or began elsewhere)
      if (immediate) {
        state_ = State::Idle;
        return Click::Single;
      }
      state_ = State::Waiting;
      release_ms_ = t_ms;
      return Click::None;
    case Gesture::Long:
      if (state_ != State::FirstDown && state_ != State::SecondDown) return Click::None;
      state_ = State::Idle;  // also discards the pending single of a double attempt
      return Click::Long;
    case Gesture::Repeat:
      return Click::None;
  }
  return Click::None;
}

Click ClickRecognizer::poll(uint32_t now_ms) {
  // Signed: a caller's clock sample taken just before the release event was
  // queued must not look like a time far in the future.
  if (state_ != State::Waiting || int32_t(now_ms - release_ms_) < int32_t(kDoublePressMs)) return Click::None;
  state_ = State::Idle;
  return Click::Single;
}

Click ClickRecognizer::flush() {
  if (state_ != State::Waiting) return Click::None;
  state_ = State::Idle;
  return Click::Single;
}

}  // namespace badge
