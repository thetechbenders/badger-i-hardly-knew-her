// Button debouncing and gesture detection.
//
// Sampling: the platform samples the raw GPIO levels every kSampleMs from a
// timer interrupt and feeds them to ButtonTracker::sample(). A button counts
// as pressed after kDebounceMs of consistently "pressed" samples, and as
// released after kDebounceMs of consistently "released" samples.
//
// Raw gestures (per button, from ButtonTracker in the timer ISR):
//   Press   - emitted once when the debounced press begins.
//   Short   - emitted on release when the hold lasted < kLongPressMs.
//             `count` is the number of Repeat events the hold produced.
//   Long    - emitted once, while still held, when the hold reaches
//             kLongPressMs. The following release emits nothing.
//   Repeat  - UP/DOWN only (kRepeatMask): while held, first after
//             kRepeatDelayMs, then every kRepeatIntervalMs, also past the
//             long-press threshold. `count` is 1, 2, ... (saturating).
//   Hold    - DOWN only (kHoldMask): emitted once, while still held, when
//             the hold reaches kHoldPowerOffMs (after Long). The project
//             index uses it for power-off, where Long is taken by scrolling.
// Buttons already held at boot (the wake button on battery) are ignored
// until they have been released once, so waking never triggers a long press.
//
// Recognised C actions (ClickRecognizer, on core 0, see below): Single,
// Double and Long, one per interaction.
#pragma once

#include <cstdint>

namespace badge {

enum class Button : uint8_t { A = 0, B, C, Up, Down, User, Count };
constexpr int kButtonCount = int(Button::Count);

// Values are stable (tests); Press, Repeat and Hold were appended.
enum class Gesture : uint8_t { Short, Long, Press, Repeat, Hold };

struct ButtonEvent {
  Button button;
  Gesture gesture;
  uint32_t t_ms;
  uint8_t count = 0;  // Repeat: n-th repeat; Short: repeats during the hold
};

// ---------------------------------------------------------------- timing
// All thresholds in milliseconds of debounced (stable) button state.
constexpr uint32_t kSampleMs = 5;
constexpr uint32_t kDebounceMs = 20;
// Hold time for a long press (all buttons).
constexpr uint32_t kLongPressMs = 1000;
// UP/DOWN auto-repeat: half the long-press time before the first repeat, so
// a deliberate hold starts moving well before it would count as long, then
// ~6.7 steps per second (12 entries in under 2 s).
constexpr uint32_t kRepeatDelayMs = 500;
constexpr uint32_t kRepeatIntervalMs = 150;
// Double press: the second C press must begin less than this long after the
// first release. This is also how long a single short C waits before it
// acts outside the project index (inside the index it acts at once).
// About a third of the long-press time: comfortable for a deliberate double
// press, short next to an e-paper refresh.
constexpr uint32_t kDoublePressMs = 350;

// Power-off hold inside the project index: three long presses. Holding DOWN
// scrolls all 12 entries in kRepeatDelayMs + 11 * kRepeatIntervalMs (2.15 s),
// so a scroll to the end does not power off by itself.
constexpr uint32_t kHoldPowerOffMs = 3 * kLongPressMs;
static_assert(kHoldPowerOffMs > kRepeatDelayMs + 11 * kRepeatIntervalMs, "scrolling 12 entries must not power off");

constexpr uint32_t kRepeatMask = (1u << int(Button::Up)) | (1u << int(Button::Down));
constexpr uint32_t kHoldMask = 1u << int(Button::Down);
// Upper bound of events one sample() call can produce per button: Press or
// Short, plus Repeat, Long and Hold (which can coincide).
constexpr int kMaxEventsPerSample = 4 * kButtonCount;

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
    bool hold_fired = false;
    bool suppressed = false;
    uint8_t repeats = 0;
    uint32_t down_ms = 0;
    uint32_t next_repeat_ms = 0;
  };
  static constexpr uint8_t kSteps = kDebounceMs / kSampleMs;
  State st_[kButtonCount];
  uint32_t stable_ = 0;
};

const char *button_name(Button b);

// ------------------------------------------------------- C press decoding
// Turns the raw C gestures into exactly one action per interaction:
//   Single  short press, no second press within kDoublePressMs. When the
//           caller says `immediate` (inside the project index), it fires on
//           release; otherwise it is deferred until the window has expired,
//           so a double press never performs the single action first.
//   Double  second press began within kDoublePressMs of the first release
//           (fires on the second release).
//   Long    hold reached kLongPressMs on the first or second press; any
//           pending single is discarded and the release emits nothing.
// Events that do not belong to an interaction that started with a Press seen
// by this recognizer (after reset() or cancel()) are ignored, so a press
// that began on another screen never fires late.
//
// Contract: call poll(now) before on_event(e) with now = e.t_ms, and
// periodically, so an expired single fires before any later event.
enum class Click : uint8_t { None, Single, Double, Long };
const char *click_name(Click c);

class ClickRecognizer {
 public:
  void reset() { state_ = State::Idle; }
  Click on_event(Gesture g, uint32_t t_ms, bool immediate);
  // Fires a deferred Single once the double-press window has expired.
  Click poll(uint32_t now_ms);
  // Context changed under a pending interaction: drop it, and ignore the
  // rest of a press that is still held.
  void cancel() { state_ = State::Idle; }
  // Another input arrived: a deferred Single happens now (before that input).
  Click flush();
  bool pending() const { return state_ == State::Waiting; }
  bool active() const { return state_ != State::Idle; }

 private:
  enum class State : uint8_t { Idle, FirstDown, Waiting, SecondDown };
  State state_ = State::Idle;
  uint32_t release_ms_ = 0;
};

}  // namespace badge
