#include "check.hpp"
#include "input.hpp"

using namespace badge;

namespace {
struct Rig {
  ButtonTracker bt;
  uint32_t t = 0;
  ButtonEvent ev[16];
  int total = 0;
  ButtonEvent last{};
  void run(uint32_t mask, uint32_t ms) {
    for (uint32_t i = 0; i < ms; i += kSampleMs) {
      t += kSampleMs;
      int n = bt.sample(mask, t, ev, 16);
      total += n;
      if (n) last = ev[n - 1];
    }
  }
};
const uint32_t A = 1u << int(Button::A), B = 1u << int(Button::B), DOWN = 1u << int(Button::Down);
}  // namespace

TEST(input_bounce_shorter_than_debounce_is_ignored) {
  Rig r;
  r.bt.reset(0);
  for (int i = 0; i < 20; ++i) { r.run(A, 5); r.run(0, 5); }  // 5 ms chatter
  r.run(0, 100);
  CHECK_EQ(r.total, 0);
}

TEST(input_short_press_emits_on_release) {
  Rig r;
  r.bt.reset(0);
  r.run(A, 200);
  CHECK_EQ(r.total, 0);
  r.run(0, 50);
  CHECK_EQ(r.total, 1);
  CHECK(r.last.button == Button::A && r.last.gesture == Gesture::Short);
}

TEST(input_long_press_fires_once_without_short) {
  Rig r;
  r.bt.reset(0);
  r.run(B, kLongPressMs + 100);
  CHECK_EQ(r.total, 1);
  CHECK(r.last.button == Button::B && r.last.gesture == Gesture::Long);
  r.run(B, 3000);
  r.run(0, 100);
  CHECK_EQ(r.total, 1);
}

TEST(input_wake_button_is_ignored_until_released) {
  Rig r;
  r.bt.reset(DOWN);  // held while powering on
  r.run(DOWN, 5000);  // user keeps holding: must NOT trigger long-press power-off
  r.run(0, 100);
  CHECK_EQ(r.total, 0);
  r.run(DOWN, 100);  // a fresh press afterwards works
  r.run(0, 100);
  CHECK_EQ(r.total, 1);
}

TEST(input_simultaneous_buttons_are_independent) {
  Rig r;
  r.bt.reset(0);
  r.run(A | B, 100);
  r.run(B, 100);
  CHECK_EQ(r.total, 1);  // A released
  r.run(0, 100);
  CHECK_EQ(r.total, 2);
}
