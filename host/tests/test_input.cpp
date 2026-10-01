#include <vector>

#include "app.hpp"
#include "check.hpp"
#include "input.hpp"

using namespace badge;

namespace {
struct Rig {
  ButtonTracker bt;
  uint32_t t = 0;
  std::vector<ButtonEvent> all;
  int total = 0;  // everything except Press (the pre-existing gestures)
  ButtonEvent last{};
  void run(uint32_t mask, uint32_t ms) {
    ButtonEvent ev[kMaxEventsPerSample];
    for (uint32_t i = 0; i < ms; i += kSampleMs) {
      t += kSampleMs;
      int n = bt.sample(mask, t, ev, kMaxEventsPerSample);
      for (int k = 0; k < n; ++k) {
        all.push_back(ev[k]);
        if (ev[k].gesture != Gesture::Press) {
          ++total;
          last = ev[k];
        }
      }
    }
  }
  int count(Button b, Gesture g) const {
    int n = 0;
    for (const auto &e : all) n += e.button == b && e.gesture == g;
    return n;
  }
};
const uint32_t A = 1u << int(Button::A), B = 1u << int(Button::B), C = 1u << int(Button::C),
               UP = 1u << int(Button::Up), DOWN = 1u << int(Button::Down);
}  // namespace

TEST(input_bounce_shorter_than_debounce_is_ignored) {
  Rig r;
  r.bt.reset(0);
  for (int i = 0; i < 20; ++i) { r.run(A, 5); r.run(0, 5); }  // 5 ms chatter
  r.run(0, 100);
  CHECK_EQ(r.all.size(), 0u);  // not even a Press
}

TEST(input_short_press_emits_on_release) {
  Rig r;
  r.bt.reset(0);
  r.run(A, 200);
  CHECK_EQ(r.total, 0);
  CHECK_EQ(r.count(Button::A, Gesture::Press), 1);  // once, after the debounce time
  CHECK_EQ(r.all[0].t_ms, kDebounceMs);
  r.run(0, 50);
  CHECK_EQ(r.total, 1);
  CHECK(r.last.button == Button::A && r.last.gesture == Gesture::Short);
  CHECK_EQ(r.last.count, 0);
}

TEST(input_long_press_fires_once_without_short) {
  Rig r;
  r.bt.reset(0);
  r.run(B, kLongPressMs + 100);
  CHECK_EQ(r.total, 1);
  CHECK(r.last.button == Button::B && r.last.gesture == Gesture::Long);
  r.run(B, 3000);
  r.run(0, 100);
  CHECK_EQ(r.total, 1);  // release after a long press: nothing
}

TEST(input_long_press_boundary) {
  // Long fires when the debounced hold reaches kLongPressMs, not one sample earlier.
  Rig r;
  r.bt.reset(0);
  r.run(C, kDebounceMs);  // debounced press at t = kDebounceMs
  const uint32_t down = r.t;
  r.run(C, kLongPressMs - kSampleMs);
  CHECK_EQ(r.count(Button::C, Gesture::Long), 0);
  r.run(C, kSampleMs);
  CHECK_EQ(r.count(Button::C, Gesture::Long), 1);
  CHECK_EQ(r.last.t_ms - down, kLongPressMs);
  // Released one sample before the threshold instead: a short press. (The
  // debounce delays press and release alike, so the raw hold time counts.)
  Rig s;
  s.bt.reset(0);
  s.run(C, kLongPressMs - kSampleMs);
  s.run(0, 50);
  CHECK_EQ(s.count(Button::C, Gesture::Short), 1);
  CHECK_EQ(s.count(Button::C, Gesture::Long), 0);
}

TEST(input_wake_button_is_ignored_until_released) {
  Rig r;
  r.bt.reset(DOWN);  // held while powering on
  r.run(DOWN, 5000);  // user keeps holding: must NOT trigger long-press power-off
  r.run(0, 100);
  CHECK_EQ(r.all.size(), 0u);  // no Press, Repeat, Long or Short at all
  r.run(DOWN, 100);  // a fresh press afterwards works
  r.run(0, 100);
  CHECK_EQ(r.total, 1);
  CHECK_EQ(r.count(Button::Down, Gesture::Press), 1);
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

TEST(input_up_down_repeat_while_held) {
  Rig r;
  r.bt.reset(0);
  r.run(DOWN, kDebounceMs + kRepeatDelayMs - kSampleMs);
  CHECK_EQ(r.count(Button::Down, Gesture::Repeat), 0);  // not before the delay
  r.run(DOWN, kSampleMs);
  CHECK_EQ(r.count(Button::Down, Gesture::Repeat), 1);
  r.run(DOWN, 3 * kRepeatIntervalMs);
  CHECK_EQ(r.count(Button::Down, Gesture::Repeat), 4);
  CHECK_EQ(r.last.count, 4);
  // Repeats keep coming past the long-press threshold; Long fires once.
  r.run(DOWN, 2000);
  CHECK_EQ(r.count(Button::Down, Gesture::Long), 1);
  const int reps = r.count(Button::Down, Gesture::Repeat);
  CHECK(reps > 4 + 2000 / int(kRepeatIntervalMs) - 2);
  r.run(0, 100);
  CHECK_EQ(r.count(Button::Down, Gesture::Short), 0);  // long: no release event
  // A hold that repeated but stayed short reports its repeats on release.
  Rig s;
  s.bt.reset(0);
  s.run(UP, kDebounceMs + kRepeatDelayMs + kRepeatIntervalMs);
  s.run(0, 50);
  CHECK_EQ(s.count(Button::Up, Gesture::Short), 1);
  CHECK_EQ(s.last.count, 2);
  // Other buttons never repeat.
  Rig o;
  o.bt.reset(0);
  o.run(A | B | C, 3000);
  CHECK_EQ(o.count(Button::A, Gesture::Repeat) + o.count(Button::B, Gesture::Repeat) +
               o.count(Button::C, Gesture::Repeat), 0);
}

TEST(input_events_per_sample_bound) {
  // Every button pressed in the same sample, then all repeat/long at once.
  Rig r;
  r.bt.reset(0);
  ButtonEvent ev[kMaxEventsPerSample];
  int worst = 0;
  for (uint32_t t = 5; t < 3000; t += 5) {
    const int n = r.bt.sample(0x3F, t, ev, kMaxEventsPerSample);
    worst = n > worst ? n : worst;
  }
  CHECK(worst <= kMaxEventsPerSample);
  CHECK(worst >= 2);  // UP + DOWN Repeat and Long can coincide
}

// ------------------------------------------------------------- C recognizer

namespace {
struct Clicks {
  ClickRecognizer cr;
  uint32_t t = 1000;
  std::vector<Click> out;
  void feed(Gesture g, bool immediate = false) {
    Click c = cr.poll(t);
    if (c != Click::None) out.push_back(c);
    c = cr.on_event(g, t, immediate);
    if (c != Click::None) out.push_back(c);
  }
  void wait(uint32_t ms) {
    for (uint32_t i = 0; i < ms; ++i) {
      ++t;
      const Click c = cr.poll(t);
      if (c != Click::None) out.push_back(c);
    }
  }
  void tap(uint32_t hold = 80, bool immediate = false) {
    feed(Gesture::Press, immediate);
    t += hold;
    feed(Gesture::Short, immediate);
  }
};
}  // namespace

TEST(click_single_waits_for_the_double_press_window) {
  Clicks k;
  k.tap();
  CHECK(k.cr.pending());
  k.wait(kDoublePressMs - 1);
  CHECK_EQ(k.out.size(), 0u);  // still waiting one ms before the window ends
  k.wait(1);
  CHECK_EQ(k.out.size(), 1u);
  CHECK(k.out[0] == Click::Single);
  CHECK(!k.cr.active());
  k.wait(5000);
  CHECK_EQ(k.out.size(), 1u);  // exactly one action
}

TEST(click_double_press_boundaries) {
  // Second press 1 ms inside the window: double, and no single first.
  Clicks k;
  k.tap();
  k.t += kDoublePressMs - 1;
  k.tap();
  k.wait(2000);
  CHECK_EQ(k.out.size(), 1u);
  CHECK(k.out[0] == Click::Double);
  // Exactly at the window end: the first was a single, the second starts anew.
  Clicks m;
  m.tap();
  m.t += kDoublePressMs;
  m.tap();
  m.wait(2000);
  CHECK_EQ(m.out.size(), 2u);
  CHECK(m.out[0] == Click::Single && m.out[1] == Click::Single);
  // A slow second release still counts: the window is about when it began.
  Clicks s;
  s.tap();
  s.t += 100;
  s.tap(kLongPressMs - 50);
  CHECK_EQ(s.out.size(), 1u);
  CHECK(s.out[0] == Click::Double);
}

TEST(click_triple_press_is_double_then_single) {
  Clicks k;
  k.tap();
  k.t += 100;
  k.tap();
  k.t += 100;
  k.tap();
  k.wait(2000);
  CHECK_EQ(k.out.size(), 2u);
  CHECK(k.out[0] == Click::Double && k.out[1] == Click::Single);
}

TEST(click_long_suppresses_everything_else) {
  Clicks k;
  k.feed(Gesture::Press);
  k.t += kLongPressMs;
  k.feed(Gesture::Long);
  CHECK_EQ(k.out.size(), 1u);
  CHECK(k.out[0] == Click::Long);
  // The tracker sends nothing on release; a stray Short would also be ignored.
  k.feed(Gesture::Short);
  k.wait(2000);
  CHECK_EQ(k.out.size(), 1u);
  // Long on the second press of a double attempt: only the long action.
  Clicks d;
  d.tap();
  d.t += 100;
  d.feed(Gesture::Press);
  d.t += kLongPressMs;
  d.feed(Gesture::Long);
  d.wait(2000);
  CHECK_EQ(d.out.size(), 1u);
  CHECK(d.out[0] == Click::Long);
}

TEST(click_immediate_mode_does_not_wait) {
  Clicks k;
  k.tap(80, true);
  CHECK_EQ(k.out.size(), 1u);
  CHECK(k.out[0] == Click::Single);
  CHECK(!k.cr.active());
  // Two quick taps in immediate mode: two singles (the caller decides what a
  // second one means; the index has been left by then).
  k.t += 100;
  k.tap(80, true);
  CHECK_EQ(k.out.size(), 2u);
}

TEST(click_cancel_and_flush) {
  Clicks k;
  k.tap();
  k.cr.cancel();
  k.wait(2000);
  CHECK_EQ(k.out.size(), 0u);  // cancelled: never fires
  // Cancel while the button is still down: its release is ignored too.
  k.feed(Gesture::Press);
  k.cr.cancel();
  k.t += 80;
  k.feed(Gesture::Short);
  k.wait(2000);
  CHECK_EQ(k.out.size(), 0u);
  // Flush makes a waiting single happen now, once.
  k.tap();
  CHECK(k.cr.flush() == Click::Single);
  CHECK(k.cr.flush() == Click::None);
  k.wait(2000);
  CHECK_EQ(k.out.size(), 0u);
  // A release with no press seen (began before a reset): ignored.
  ClickRecognizer cr;
  cr.reset();
  CHECK(cr.on_event(Gesture::Short, 10, false) == Click::None);
  CHECK(cr.on_event(Gesture::Long, 20, false) == Click::None);
  CHECK(!cr.active());
}

TEST(click_poll_with_an_older_clock_sample_never_fires_early) {
  ClickRecognizer cr;
  cr.on_event(Gesture::Press, 5000, false);
  cr.on_event(Gesture::Short, 5100, false);
  CHECK(cr.poll(5090) == Click::None);  // main loop's clock read before the event was queued
  CHECK(cr.poll(5100 + kDoublePressMs - 1) == Click::None);
  CHECK(cr.poll(5100 + kDoublePressMs) == Click::Single);
}

TEST(click_end_to_end_from_raw_samples) {
  // Debounced GPIO samples -> tracker -> recognizer, including contact bounce.
  Rig r;
  r.bt.reset(0);
  ClickRecognizer cr;
  std::vector<Click> out;
  size_t fed = 0;
  auto pump = [&](uint32_t mask, uint32_t ms) {
    for (uint32_t i = 0; i < ms; i += kSampleMs) {
      r.run(mask, kSampleMs);
      for (; fed < r.all.size(); ++fed) {
        Click c = cr.poll(r.all[fed].t_ms);
        if (c != Click::None) out.push_back(c);
        if (r.all[fed].button != Button::C) continue;
        c = cr.on_event(r.all[fed].gesture, r.all[fed].t_ms, false);
        if (c != Click::None) out.push_back(c);
      }
      const Click c = cr.poll(r.t);
      if (c != Click::None) out.push_back(c);
    }
  };
  for (int i = 0; i < 3; ++i) { pump(C, 5); pump(0, 5); }  // bounce on contact
  pump(C, 90);
  pump(0, 120);
  for (int i = 0; i < 3; ++i) { pump(C, 5); pump(0, 5); }
  pump(C, 90);
  pump(0, 1000);
  CHECK_EQ(out.size(), 1u);
  CHECK(out[0] == Click::Double);
  pump(C, 120);
  pump(0, 1000);
  CHECK_EQ(out.size(), 2u);
  CHECK(out[1] == Click::Single);
  pump(C, kLongPressMs + 500);
  pump(0, 1000);
  CHECK_EQ(out.size(), 3u);
  CHECK(out[2] == Click::Long);
}

TEST(input_down_hold_event_for_index_power_off) {
  Rig r;
  r.bt.reset(0);
  r.run(DOWN, kDebounceMs);
  const uint32_t down = r.t;
  r.run(DOWN, kHoldPowerOffMs - kSampleMs);
  CHECK_EQ(r.count(Button::Down, Gesture::Long), 1);  // at 1 s, as before
  CHECK_EQ(r.count(Button::Down, Gesture::Hold), 0);  // not one sample early
  r.run(DOWN, kSampleMs);
  CHECK_EQ(r.count(Button::Down, Gesture::Hold), 1);
  CHECK_EQ(r.all.back().t_ms - down, kHoldPowerOffMs);
  r.run(DOWN, 3000);
  r.run(0, 100);
  CHECK_EQ(r.count(Button::Down, Gesture::Hold), 1);   // once
  CHECK_EQ(r.count(Button::Down, Gesture::Short), 0);  // nothing on release
  // Only DOWN has it; a wake-suppressed DOWN never sends it.
  Rig o;
  o.bt.reset(DOWN);
  o.run(A | B | C | UP | DOWN | (1u << int(Button::User)), 5000);
  int holds = 0;
  for (const auto &e : o.all) holds += e.gesture == Gesture::Hold;
  CHECK_EQ(holds, 0);
}

TEST(input_long_usr_sends_no_short) {
  Rig r;
  r.bt.reset(0);
  const uint32_t USR = 1u << int(Button::User);
  r.run(USR, kLongPressMs + 200);
  r.run(0, 100);
  CHECK_EQ(r.count(Button::User, Gesture::Long), 1);
  CHECK_EQ(r.count(Button::User, Gesture::Short), 0);
}
