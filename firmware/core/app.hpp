// Application state machine: turns button gestures, CLI requests and power
// events into the desired View and into side-effect requests for the
// platform (refresh, sleep). Pure logic; no hardware access.
//
// Controls (outside the project index)
//   A short   photo badge              A long    clean full refresh (de-ghost)
//   B short   business card            B long    contact QR (if configured); on a
//                                                project page: that project's QR
//                                                (only if it has a URL); again: back
//   C short   portfolio at the last-viewed project (this session); acts once
//             the double-press window (kDoublePressMs) has passed. Another
//             button pressed in that window decides what happens to it:
//               applies first (one frame): B long -> that project's QR (or
//                 its page if it has no link), UP/DOWN short -> step from it,
//                 A long -> clean refresh of it, UP long / USR long -> still
//                 toggles gesture mode / layout
//               dropped (never drawn): A short, B short, USR short, DOWN
//                 long (power off), any swipe, any USB screen/content change
//   C double  project index, highlighting the current / last-viewed project
//   C long    project 1 (from anywhere, including a project QR)
//   UP/DOWN   previous/next project on the portfolio; from a QR screen,
//             return to the page it came from (card / same project)
//   UP long   gesture mode on/off (APDS-9960 on Qwiic; not saved)
//   DOWN long power off now (battery) / emulated sleep (USB)
//   USR short diagnostics screen       USR long  toggle layout candidate for
//                                                this session (not saved)
//
// Project index (modal list of project names; session-only state)
//   UP/DOWN   move the highlight (wraps); holding repeats (stops at the ends)
//   DOWN held kHoldPowerOffMs (3 s): power off (1 s is taken by scrolling);
//             the highlight is not confirmed. Holds begun before the index
//             opened are ignored.
//   C short   open the highlighted project at once (it becomes the
//             remembered project)
//   C long    project 1               A short   cancel: back to the previous
//                                                screen, remembered project kept
//   A long    clean full refresh       B, USR, swipes: ignored; C double: no-op
//   Quiet browsing: the highlight moves in RAM; the index is redrawn only
//   after the UP/DOWN buttons have been quiet for kIndexSettleMs (release, or
//   a pause between taps), never per repeat, and only if it changed.
//
// Safe mode (Recovery screen): only C long / USR short (diagnostics).
//
// Gesture mode (swipes, only while enabled; buttons always work)
//   left / right  previous / next screen in badge -> card -> projects
//   up            business card; again from the card: full-screen QR
//   down          photo badge
#pragma once

#include <cstdint>

#include "gesture.hpp"
#include "input.hpp"

namespace badge {

// Values are stable (CLI names, tests); ProjectQr and Index were appended.
enum class Screen : uint8_t { Badge = 0, Card, Projects, QrFull, Info, Recovery, ProjectQr, Index, Count };
const char *screen_name(Screen s);
bool screen_from_name(const char *name, Screen *out);

// Rows of project names the index shows at once (renderer layout).
constexpr int kIndexRows = 7;
// Quiet browsing: the index is redrawn once UP/DOWN have produced no event
// for this long. Longer than kRepeatIntervalMs, so a held button never
// redraws; short enough to feel like "on release".
constexpr uint32_t kIndexSettleMs = 300;
static_assert(kIndexSettleMs > kRepeatIntervalMs, "a held button must not trigger index redraws");

// First visible index row so that `sel` is visible, moving the window as
// little as possible from `top` (the list only scrolls at its edges).
int index_viewport_top(int top, int sel, int count, int rows = kIndexRows);

struct View {
  Screen screen = Screen::Badge;
  uint8_t project = 0;    // n-th configured project: the remembered (last-viewed) one
  uint8_t layout = 0;     // effective layout candidate
  uint8_t index_sel = 0;  // index: highlighted entry as drawn (Screen::Index only)
  uint8_t index_top = 0;  // index: first visible row
  bool operator==(const View &o) const {
    return screen == o.screen && project == o.project && layout == o.layout && index_sel == o.index_sel &&
           index_top == o.index_top;
  }
  bool operator!=(const View &o) const { return !(*this == o); }
};

enum Action : uint32_t {
  kActNone = 0,
  kActRedraw = 1u << 0,       // view or content changed
  kActCleanRefresh = 1u << 1, // force a full clean refresh
  kActSleep = 1u << 2,        // begin the power-off sequence
  kActGestureMode = 1u << 3,  // gesture mode changed: start/stop the sensor
};

struct AppConfig {
  uint8_t project_count = 0;
  uint16_t project_url_mask = 0;  // bit n: n-th configured project has an https URL
  bool qr_configured = false;
  uint8_t layout = 0;
  bool safe_mode = false;
  uint16_t sleep_timeout_s = 0;
  bool sleep_to_badge = true;
  bool wake_selects_screen = true;
  bool gesture_default_on = false;
  uint16_t gesture_timeout_s = 0;
};

class App {
 public:
  // `wake_button` is the button that powered the board on (or -1).
  void boot(const AppConfig &cfg, int wake_button, uint32_t now_ms);
  // Raw gestures from the ButtonTracker, in order.
  uint32_t on_button(const ButtonEvent &e);
  // Time-driven work, called every main-loop pass: a deferred single C, and
  // the quiet-browsing redraw of the index.
  uint32_t on_poll(uint32_t now_ms);
  // From the CLI. For Screen::Index, `project` is the entry to highlight.
  uint32_t on_screen_request(Screen s, int project = -1);
  uint32_t on_project_step(int delta);
  uint32_t on_swipe(Swipe s, uint32_t now_ms);
  uint32_t set_gesture_mode(bool on);
  uint32_t on_config_changed(const AppConfig &cfg);  // content/prefs changed
  uint32_t on_activity(uint32_t now_ms);             // any user/USB activity
  // Periodic check; `on_battery` gates auto power-off.
  uint32_t on_tick(uint32_t now_ms, bool on_battery, bool display_settled);

  const View &view() const { return view_; }
  // Index: the highlighted entry in RAM (may be ahead of view().index_sel
  // until the quiet-browsing redraw).
  int index_selection() const { return index_sel_; }
  bool index_redraw_pending() const { return index_dirty_; }
  bool click_pending() const { return click_.pending(); }
  bool click_frozen() const { return c_frozen_; }
  bool sleeping() const { return sleep_pending_; }
  bool gesture_mode() const { return gesture_on_ && !sleep_pending_; }
  // View to show right before power-off (badge or the current one).
  View sleep_view() const;
  uint32_t idle_ms(uint32_t now_ms) const { return now_ms - last_activity_ms_; }

 private:
  uint32_t go(Screen s);
  uint32_t on_click(Click c);
  uint32_t on_index_button(const ButtonEvent &e);
  uint32_t poll_click(uint32_t now_ms);
  static bool pending_c_applies_first(const ButtonEvent &e);
  uint32_t power_off();
  uint32_t open_project(int n);
  uint32_t open_index(int sel);
  uint32_t publish_index();
  void index_step(int delta, bool wrap);
  Screen sanitized(Screen s) const;
  bool project_has_url(int n) const { return n >= 0 && n < 16 && (cfg_.project_url_mask >> n) & 1u; }
  uint32_t on_tick_power(uint32_t now_ms, bool on_battery, bool display_settled);
  AppConfig cfg_;
  View view_;
  bool layout_toggled_ = false;
  bool sleep_pending_ = false;
  bool gesture_on_ = false;
  uint32_t last_activity_ms_ = 0;
  ClickRecognizer click_;
  // A pending single C is frozen while another button decides its fate.
  static constexpr uint32_t kPendingCSlackMs = 250;
  bool c_frozen_ = false;
  uint32_t c_frozen_until_ = 0;
  bool nav_fresh_[2] = {false, false};  // UP, DOWN: hold began in the index
  // Index (session-only, never saved): live highlight, screen to return to
  // on cancel, and the quiet-browsing redraw timer.
  uint8_t index_sel_ = 0;
  Screen index_return_ = Screen::Badge;
  bool index_dirty_ = false;  // index_sel_ not yet drawn
  uint32_t index_quiet_until_ = 0;
};

}  // namespace badge
