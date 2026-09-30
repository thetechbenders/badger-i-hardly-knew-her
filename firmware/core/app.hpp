// Application state machine: turns button gestures, CLI requests and power
// events into the desired View and into side-effect requests for the
// platform (refresh, sleep). Pure logic; no hardware access.
//
// Controls
//   A short   photo badge              A long    clean full refresh (de-ghost)
//   B short   business card            B long    full-screen QR (if configured)
//   C short   projects (if any)        C long    diagnostics screen
//   UP/DOWN   previous/next project on the projects screen; from the
//             full-screen QR, either returns to the card
//   UP long   gesture mode on/off (APDS-9960 on Qwiic; not saved)
//   DOWN long power off now (battery) / emulated sleep (USB)
//   USR long  toggle layout candidate for this session (not saved)
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

enum class Screen : uint8_t { Badge = 0, Card, Projects, QrFull, Info, Recovery, Count };
const char *screen_name(Screen s);
bool screen_from_name(const char *name, Screen *out);

struct View {
  Screen screen = Screen::Badge;
  uint8_t project = 0;  // n-th configured project
  uint8_t layout = 0;   // effective layout candidate
  bool operator==(const View &o) const {
    return screen == o.screen && project == o.project && layout == o.layout;
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
  uint32_t on_button(const ButtonEvent &e);
  uint32_t on_screen_request(Screen s, int project = -1);  // from the CLI
  uint32_t on_project_step(int delta);
  uint32_t on_swipe(Swipe s, uint32_t now_ms);
  uint32_t set_gesture_mode(bool on);
  uint32_t on_config_changed(const AppConfig &cfg);  // content/prefs changed
  uint32_t on_activity(uint32_t now_ms);             // any user/USB activity
  // Periodic check; `on_battery` gates auto power-off.
  uint32_t on_tick(uint32_t now_ms, bool on_battery, bool display_settled);

  const View &view() const { return view_; }
  bool sleeping() const { return sleep_pending_; }
  bool gesture_mode() const { return gesture_on_ && !sleep_pending_; }
  // View to show right before power-off (badge or the current one).
  View sleep_view() const;
  uint32_t idle_ms(uint32_t now_ms) const { return now_ms - last_activity_ms_; }

 private:
  uint32_t go(Screen s);
  uint32_t on_tick_power(uint32_t now_ms, bool on_battery, bool display_settled);
  AppConfig cfg_;
  View view_;
  bool layout_toggled_ = false;
  bool sleep_pending_ = false;
  bool gesture_on_ = false;
  uint32_t last_activity_ms_ = 0;
};

}  // namespace badge
