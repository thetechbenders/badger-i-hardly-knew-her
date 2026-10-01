#include "app.hpp"
#include "check.hpp"

using namespace badge;

namespace {
AppConfig cfg(int projects = 2, bool qr = true) {
  AppConfig c;
  c.project_count = uint8_t(projects);
  c.qr_configured = qr;
  c.sleep_timeout_s = 60;
  return c;
}
ButtonEvent press(Button b, Gesture g = Gesture::Short, uint32_t t = 1000) { return {b, g, t}; }

// A complete debounced interaction as the ButtonTracker reports it. The
// clock only moves forward, so deferred C actions and the quiet-browsing
// timer behave as on the device.
struct Clock {
  uint32_t t = 1000;
};
Clock g_clock;
uint32_t ev(App &a, Button b, Gesture g, uint8_t count = 0) { return a.on_button({b, g, g_clock.t, count}); }
uint32_t settle(App &a, uint32_t ms) {
  g_clock.t += ms;
  return a.on_poll(g_clock.t);
}
// Short press and release; for C, also wait out the double-press window.
uint32_t tap(App &a, Button b, bool wait = true) {
  uint32_t r = ev(a, b, Gesture::Press);
  g_clock.t += 80;
  r |= ev(a, b, Gesture::Short);
  if (b == Button::C && wait) r |= settle(a, kDoublePressMs);
  return r;
}
uint32_t hold(App &a, Button b) {
  uint32_t r = ev(a, b, Gesture::Press);
  g_clock.t += kLongPressMs;
  return r | ev(a, b, Gesture::Long);  // the release that follows emits nothing
}
uint32_t double_c(App &a) {
  uint32_t r = tap(a, Button::C, false);
  g_clock.t += kDoublePressMs - 100;
  r |= ev(a, Button::C, Gesture::Press);
  g_clock.t += 80;
  return r | ev(a, Button::C, Gesture::Short);
}
}  // namespace

TEST(app_boots_to_badge_and_wake_button_selects) {
  App a;
  a.boot(cfg(), -1, 0);
  CHECK(a.view().screen == Screen::Badge);
  a.boot(cfg(), int(Button::B), 0);
  CHECK(a.view().screen == Screen::Card);
  a.boot(cfg(0), int(Button::C), 0);  // no projects configured
  CHECK(a.view().screen == Screen::Badge);
  AppConfig c = cfg();
  c.wake_selects_screen = false;
  a.boot(c, int(Button::B), 0);
  CHECK(a.view().screen == Screen::Badge);
}

TEST(app_button_navigation) {
  App a;
  a.boot(cfg(), -1, 0);
  CHECK_EQ(a.on_button(press(Button::A)), kActNone);  // already there
  CHECK_EQ(a.on_button(press(Button::B)), kActRedraw);
  CHECK(a.view().screen == Screen::Card);
  CHECK_EQ(tap(a, Button::C), kActRedraw);
  CHECK(a.view().screen == Screen::Projects);
  CHECK_EQ(a.on_button(press(Button::Down)), kActRedraw);
  CHECK_EQ(a.view().project, 1);
  a.on_button(press(Button::Down));
  CHECK_EQ(a.view().project, 0);  // wraps
  a.on_button(press(Button::Up));
  CHECK_EQ(a.view().project, 1);
  CHECK_EQ(a.on_button(press(Button::A)), kActRedraw);
  CHECK(a.view().screen == Screen::Badge);
  CHECK_EQ(a.on_button(press(Button::Down)), kActNone);  // up/down do nothing on the badge
}

TEST(app_long_presses) {
  App a;
  a.boot(cfg(), -1, 0);
  CHECK_EQ(a.on_button(press(Button::A, Gesture::Long)), kActRedraw | kActCleanRefresh);
  a.on_button(press(Button::B, Gesture::Long));
  CHECK(a.view().screen == Screen::QrFull);
  a.on_button(press(Button::Up));
  CHECK(a.view().screen == Screen::Card);
  CHECK_EQ(tap(a, Button::User), kActRedraw);  // diagnostics moved to USR short
  CHECK(a.view().screen == Screen::Info);
  CHECK_EQ(hold(a, Button::C), kActRedraw);  // long C: project 1
  CHECK(a.view().screen == Screen::Projects && a.view().project == 0);
  a.boot(cfg(2, false), -1, 0);
  a.on_button(press(Button::B, Gesture::Long));
  CHECK(a.view().screen == Screen::Card);  // no QR configured
  a.on_button(press(Button::A));
  const uint8_t l0 = a.view().layout;
  CHECK_EQ(a.on_button(press(Button::User, Gesture::Long)), kActRedraw);  // layout toggle moved to USR
  CHECK(a.view().layout != l0);
  CHECK_EQ(a.on_button(press(Button::Up, Gesture::Long)), kActGestureMode | kActRedraw);
  CHECK(a.gesture_mode());
  CHECK_EQ(a.on_button(press(Button::Down, Gesture::Long)), kActSleep | kActGestureMode);
  CHECK(!a.gesture_mode());  // sensor off while powering down
  CHECK(a.sleeping());
  CHECK_EQ(a.on_button(press(Button::B)), kActNone);  // ignored once committed to sleep
}

TEST(app_auto_sleep_only_on_battery_and_when_settled) {
  App a;
  a.boot(cfg(), -1, 0);
  CHECK_EQ(a.on_tick(59000, true, true), kActNone);
  CHECK_EQ(a.on_tick(61000, false, true), kActNone);  // USB: never auto sleep
  CHECK_EQ(a.on_tick(61000, true, false), kActNone);  // refresh still running
  a.on_activity(50000);
  CHECK_EQ(a.on_tick(100000, true, true), kActNone);
  CHECK_EQ(a.on_tick(110001, true, true), kActSleep);
  AppConfig c = cfg();
  c.sleep_timeout_s = 0;
  a.boot(c, -1, 0);
  CHECK_EQ(a.on_tick(1u << 30, true, true), kActNone);
}

TEST(app_sleep_view_returns_to_badge) {
  App a;
  a.boot(cfg(), -1, 0);
  a.on_button(press(Button::B));
  CHECK(a.sleep_view().screen == Screen::Badge);
  AppConfig c = cfg();
  c.sleep_to_badge = false;
  a.boot(c, -1, 0);
  a.on_button(press(Button::B));
  CHECK(a.sleep_view().screen == Screen::Card);
}

TEST(app_config_change_clamps_view) {
  App a;
  a.boot(cfg(3), -1, 0);
  a.on_screen_request(Screen::Projects, 2);
  CHECK_EQ(a.view().project, 2);
  a.on_config_changed(cfg(1));
  CHECK_EQ(a.view().project, 0);
  a.on_config_changed(cfg(0));
  CHECK(a.view().screen == Screen::Badge);
  a.on_screen_request(Screen::QrFull);
  CHECK(a.view().screen == Screen::QrFull);
  a.on_config_changed(cfg(0, false));
  CHECK(a.view().screen == Screen::Card);
}

TEST(app_safe_mode_is_sticky) {
  App a;
  AppConfig c = cfg();
  c.safe_mode = true;
  a.boot(c, int(Button::B), 0);
  CHECK(a.view().screen == Screen::Recovery);
  CHECK_EQ(a.on_button(press(Button::A)), kActNone);
  CHECK_EQ(a.on_tick(1u << 30, true, true), kActNone);  // no auto power-off in safe mode
  CHECK_EQ(tap(a, Button::C), kActNone);
  CHECK_EQ(double_c(a), kActNone);  // no index in safe mode
  CHECK(a.view().screen == Screen::Recovery);
  hold(a, Button::C);
  CHECK(a.view().screen == Screen::Info);
  a.boot(c, -1, 0);
  tap(a, Button::User);
  CHECK(a.view().screen == Screen::Info);
}

TEST(app_gesture_navigation) {
  App a;
  a.boot(cfg(2, true), -1, 0);
  CHECK_EQ(a.on_swipe(Swipe::Right, 10), kActNone);  // gesture mode off: ignored
  CHECK(a.view().screen == Screen::Badge);
  a.on_button(press(Button::Up, Gesture::Long));
  CHECK(a.gesture_mode());
  a.on_swipe(Swipe::Right, 20);
  CHECK(a.view().screen == Screen::Card);
  a.on_swipe(Swipe::Right, 30);
  CHECK(a.view().screen == Screen::Projects);
  a.on_swipe(Swipe::Right, 40);
  CHECK(a.view().screen == Screen::Badge);  // wraps
  a.on_swipe(Swipe::Left, 50);
  CHECK(a.view().screen == Screen::Projects);
  a.on_swipe(Swipe::Up, 60);
  CHECK(a.view().screen == Screen::Card);
  a.on_swipe(Swipe::Up, 70);
  CHECK(a.view().screen == Screen::QrFull);
  a.on_swipe(Swipe::Right, 80);  // QR counts as the card
  CHECK(a.view().screen == Screen::Projects);
  a.on_swipe(Swipe::Down, 90);
  CHECK(a.view().screen == Screen::Badge);
  CHECK_EQ(a.on_swipe(Swipe::Down, 95), kActNone);  // already there: no refresh
  // Buttons keep working in gesture mode.
  CHECK_EQ(a.on_button(press(Button::B)), kActRedraw);
  // No projects: the cycle is badge <-> card.
  a.on_config_changed(cfg(0, false));
  a.on_swipe(Swipe::Right, 100);
  CHECK(a.view().screen == Screen::Badge);
  a.on_swipe(Swipe::Up, 110);
  CHECK(a.view().screen == Screen::Card);
  a.on_swipe(Swipe::Up, 120);
  CHECK(a.view().screen == Screen::Card);  // no QR configured
}

TEST(app_gesture_mode_timeout_and_defaults) {
  App a;
  AppConfig c = cfg();
  c.gesture_default_on = true;
  c.gesture_timeout_s = 60;
  a.boot(c, -1, 0);
  CHECK(a.gesture_mode());
  a.on_swipe(Swipe::Right, 30000);  // swipes count as activity
  CHECK_EQ(a.on_tick(80000, false, true), kActNone);
  CHECK_EQ(a.on_tick(90001, false, true), kActGestureMode | kActRedraw);
  CHECK(!a.gesture_mode());
  c.safe_mode = true;
  a.boot(c, -1, 0);
  CHECK(!a.gesture_mode());
  CHECK_EQ(a.on_swipe(Swipe::Right, 10), kActNone);
}

namespace {
AppConfig pcfg(int projects, uint16_t url_mask) {
  AppConfig c = cfg(projects, true);
  c.project_url_mask = url_mask;
  return c;
}
}  // namespace

TEST(app_portfolio_reopens_at_last_project) {
  App a;
  a.boot(pcfg(7, 0x5F), -1, 0);
  tap(a, Button::C);
  CHECK(a.view().screen == Screen::Projects);
  CHECK_EQ(a.view().project, 0);
  for (int i = 0; i < 4; ++i) a.on_button(press(Button::Down));
  CHECK_EQ(a.view().project, 4);
  a.on_button(press(Button::A));
  a.on_button(press(Button::B));
  CHECK(a.view().screen == Screen::Card);
  CHECK_EQ(tap(a, Button::C), kActRedraw);
  CHECK(a.view().screen == Screen::Projects);
  CHECK_EQ(a.view().project, 4);  // where the visitor left off
  a.on_button(press(Button::Up));
  a.on_button(press(Button::Up));
  a.on_button(press(Button::Up));
  a.on_button(press(Button::Up));
  a.on_button(press(Button::Up));
  CHECK_EQ(a.view().project, 6);  // wraps backwards to the last entry
  CHECK_EQ(tap(a, Button::C), kActNone);  // already on the portfolio
}

TEST(app_project_qr_enter_and_return) {
  App a;
  // Projects 0..6; project 5 (the teaser) has no URL.
  a.boot(pcfg(7, 0x5F), -1, 0);
  tap(a, Button::C);
  a.on_button(press(Button::Down));
  a.on_button(press(Button::Down));
  CHECK_EQ(a.on_button(press(Button::B, Gesture::Long)), kActRedraw);
  CHECK(a.view().screen == Screen::ProjectQr);
  CHECK_EQ(a.view().project, 2);
  CHECK_EQ(a.on_button(press(Button::B, Gesture::Long)), kActRedraw);  // long B again: back
  CHECK(a.view().screen == Screen::Projects);
  CHECK_EQ(a.view().project, 2);
  a.on_button(press(Button::B, Gesture::Long));
  a.on_button(press(Button::Up));  // UP/DOWN from the QR also return to that project
  CHECK(a.view().screen == Screen::Projects);
  CHECK_EQ(a.view().project, 2);
  a.on_button(press(Button::B, Gesture::Long));
  a.on_button(press(Button::Down));
  CHECK(a.view().screen == Screen::Projects);
  CHECK_EQ(a.view().project, 2);
  // Short B still opens the contact card, long B there keeps the contact QR.
  a.on_button(press(Button::B, Gesture::Long));
  CHECK_EQ(a.on_button(press(Button::B)), kActRedraw);
  CHECK(a.view().screen == Screen::Card);
  a.on_button(press(Button::B, Gesture::Long));
  CHECK(a.view().screen == Screen::QrFull);
  // C from the contact QR reopens the portfolio at the last project, not its QR.
  tap(a, Button::C);
  CHECK(a.view().screen == Screen::Projects);
  CHECK_EQ(a.view().project, 2);
}

TEST(app_project_without_url_offers_no_qr) {
  App a;
  a.boot(pcfg(7, 0x5F), -1, 0);
  a.on_screen_request(Screen::Projects, 5);
  CHECK_EQ(a.on_button(press(Button::B, Gesture::Long)), kActNone);
  CHECK(a.view().screen == Screen::Projects);
  CHECK_EQ(a.view().project, 5);
  a.on_button(press(Button::Down));
  a.on_button(press(Button::B, Gesture::Long));
  CHECK(a.view().screen == Screen::ProjectQr);
  CHECK_EQ(a.view().project, 6);
  // No contact QR configured does not affect project QRs, and vice versa.
  AppConfig c = pcfg(7, 0);
  a.boot(c, -1, 0);
  tap(a, Button::C);
  CHECK_EQ(a.on_button(press(Button::B, Gesture::Long)), kActNone);
  a.on_button(press(Button::B));
  a.on_button(press(Button::B, Gesture::Long));
  CHECK(a.view().screen == Screen::QrFull);
}

TEST(app_config_change_never_leaves_a_stale_project_qr) {
  App a;
  a.boot(pcfg(3, 0x7), -1, 0);
  a.on_screen_request(Screen::Projects, 1);
  a.on_button(press(Button::B, Gesture::Long));
  CHECK(a.view().screen == Screen::ProjectQr);
  a.on_config_changed(pcfg(3, 0x5));  // project 2's link removed over USB
  CHECK(a.view().screen == Screen::Projects);
  CHECK_EQ(a.view().project, 1);
  a.on_screen_request(Screen::Projects, 2);
  a.on_button(press(Button::B, Gesture::Long));
  CHECK(a.view().screen == Screen::ProjectQr);
  a.on_config_changed(pcfg(1, 0x1));  // list shrank under the QR
  CHECK(a.view().screen == Screen::ProjectQr && a.view().project == 0);  // project 0 still has a URL
  CHECK(a.view().project < 1);
  a.on_config_changed(pcfg(0, 0));
  CHECK(a.view().screen == Screen::Badge);
}

TEST(app_swipes_from_project_qr) {
  App a;
  a.boot(pcfg(3, 0x7), -1, 0);
  a.on_button(press(Button::Up, Gesture::Long));
  a.on_screen_request(Screen::Projects, 1);
  a.on_button(press(Button::B, Gesture::Long));
  CHECK(a.view().screen == Screen::ProjectQr);
  a.on_swipe(Swipe::Right, 100);  // the project QR counts as the portfolio
  CHECK(a.view().screen == Screen::Badge);
  a.on_screen_request(Screen::ProjectQr, 1);
  a.on_swipe(Swipe::Left, 200);
  CHECK(a.view().screen == Screen::Card);
}

TEST(app_twelve_projects_bound) {
  App a;
  a.boot(pcfg(12, 0xFFF), -1, 0);
  tap(a, Button::C);
  for (int i = 0; i < 11; ++i) a.on_button(press(Button::Down));
  CHECK_EQ(a.view().project, 11);
  a.on_button(press(Button::Down));
  CHECK_EQ(a.view().project, 0);
  a.on_screen_request(Screen::Projects, 40);  // out-of-range request is clamped
  CHECK(a.view().project < 12);
  a.boot(pcfg(1, 0x1), -1, 0);
  tap(a, Button::C);
  CHECK_EQ(a.on_button(press(Button::Down)), kActNone);  // single entry: nothing to scroll
  CHECK_EQ(a.view().project, 0);
}
