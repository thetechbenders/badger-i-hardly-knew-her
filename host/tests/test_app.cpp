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
  CHECK_EQ(a.on_button(press(Button::C)), kActRedraw);
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
  a.on_button(press(Button::C, Gesture::Long));
  CHECK(a.view().screen == Screen::Info);
  a.boot(cfg(2, false), -1, 0);
  a.on_button(press(Button::B, Gesture::Long));
  CHECK(a.view().screen == Screen::Card);  // no QR configured
  a.on_button(press(Button::A));
  const uint8_t l0 = a.view().layout;
  CHECK_EQ(a.on_button(press(Button::Up, Gesture::Long)), kActRedraw);
  CHECK(a.view().layout != l0);
  CHECK_EQ(a.on_button(press(Button::Down, Gesture::Long)), kActSleep);
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
  a.on_button(press(Button::C, Gesture::Long));
  CHECK(a.view().screen == Screen::Info);
}
