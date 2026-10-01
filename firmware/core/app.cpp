#include "app.hpp"

#include <cstring>

namespace badge {

namespace {
const char *const kScreenNames[] = {"badge", "card", "projects", "qr", "info", "recovery", "project-qr"};
}

const char *screen_name(Screen s) {
  return uint8_t(s) < uint8_t(Screen::Count) ? kScreenNames[uint8_t(s)] : "?";
}

bool screen_from_name(const char *name, Screen *out) {
  for (uint8_t i = 0; i < uint8_t(Screen::Count); ++i) {
    if (std::strcmp(name, kScreenNames[i]) == 0) {
      *out = Screen(i);
      return true;
    }
  }
  return false;
}

void App::boot(const AppConfig &cfg, int wake_button, uint32_t now_ms) {
  cfg_ = cfg;
  view_ = View{};
  view_.layout = cfg.layout;
  layout_toggled_ = false;
  sleep_pending_ = false;
  gesture_on_ = cfg.gesture_default_on && !cfg.safe_mode;
  last_activity_ms_ = now_ms;
  if (cfg.safe_mode) {
    view_.screen = Screen::Recovery;
    return;
  }
  view_.screen = Screen::Badge;
  if (cfg.wake_selects_screen) {
    if (wake_button == int(Button::B)) view_.screen = Screen::Card;
    else if (wake_button == int(Button::C) && cfg.project_count > 0) view_.screen = Screen::Projects;
  }
}

uint32_t App::go(Screen s) {
  if (view_.screen == s) return kActNone;
  view_.screen = s;
  return kActRedraw;
}

uint32_t App::on_button(const ButtonEvent &e) {
  last_activity_ms_ = e.t_ms;
  if (sleep_pending_) return kActNone;  // committed to powering off
  if (view_.screen == Screen::Recovery) {
    // Safe mode stays up until fixed over USB and rebooted; only allow
    // viewing diagnostics.
    if (e.button == Button::C && e.gesture == Gesture::Long) return go(Screen::Info);
    return kActNone;
  }
  if (e.gesture == Gesture::Short) {
    switch (e.button) {
      case Button::A: return go(Screen::Badge);
      case Button::B: return go(Screen::Card);
      case Button::C: return cfg_.project_count > 0 ? go(Screen::Projects) : kActNone;
      case Button::Up:
      case Button::Down:
        if (view_.screen == Screen::Projects) return on_project_step(e.button == Button::Up ? -1 : 1);
        if (view_.screen == Screen::QrFull) return go(Screen::Card);
        if (view_.screen == Screen::ProjectQr) return go(Screen::Projects);  // same project
        return kActNone;
      default: return kActNone;
    }
  }
  switch (e.button) {
    case Button::A: return kActRedraw | kActCleanRefresh;
    case Button::B:
      // Project context: the project's own repository QR, never the contact one.
      if (view_.screen == Screen::ProjectQr) return go(Screen::Projects);
      if (view_.screen == Screen::Projects) return project_has_url(view_.project) ? go(Screen::ProjectQr) : kActNone;
      return cfg_.qr_configured ? go(Screen::QrFull) : go(Screen::Card);
    case Button::C: return go(Screen::Info);
    case Button::Up:
      return set_gesture_mode(!gesture_on_);
    case Button::Down:
      sleep_pending_ = true;
      return kActSleep | (gesture_on_ ? kActGestureMode : kActNone);
    case Button::User:
      layout_toggled_ = !layout_toggled_;
      view_.layout = uint8_t(cfg_.layout ^ (layout_toggled_ ? 1 : 0));
      return (view_.screen == Screen::Badge) ? kActRedraw : kActNone;
    default: return kActNone;
  }
}

uint32_t App::on_screen_request(Screen s, int project) {
  if (s == Screen::Recovery && !cfg_.safe_mode) return kActNone;
  uint32_t a = kActNone;
  if (s == Screen::Projects) {
    if (cfg_.project_count == 0) return kActNone;
    if (project >= 0 && project < cfg_.project_count && project != view_.project) {
      view_.project = uint8_t(project);
      a |= kActRedraw;
    }
  }
  if (s == Screen::QrFull && !cfg_.qr_configured) s = Screen::Card;
  if (s == Screen::ProjectQr) {
    if (project >= 0 && project < cfg_.project_count && project != view_.project) {
      view_.project = uint8_t(project);
      a |= kActRedraw;
    }
    if (!project_has_url(view_.project)) return kActNone;
  }
  return a | go(s);
}

uint32_t App::on_project_step(int delta) {
  const int n = cfg_.project_count;
  if (n <= 1) return kActNone;
  view_.project = uint8_t(((int(view_.project) + delta) % n + n) % n);
  if (view_.screen == Screen::ProjectQr) view_.screen = Screen::Projects;  // never a stale QR
  return (view_.screen == Screen::Projects) ? kActRedraw : kActNone;
}

uint32_t App::set_gesture_mode(bool on) {
  if (on == gesture_on_) return kActNone;
  gesture_on_ = on;
  return kActGestureMode | kActRedraw;  // the status indicator changes
}

uint32_t App::on_swipe(Swipe s, uint32_t now_ms) {
  if (!gesture_on_ || sleep_pending_ || cfg_.safe_mode || view_.screen == Screen::Recovery) return kActNone;
  last_activity_ms_ = now_ms;
  // Cycle of main screens; a QR screen counts as the page it belongs to.
  Screen cycle[3] = {Screen::Badge, Screen::Card, Screen::Projects};
  const int n = cfg_.project_count > 0 ? 3 : 2;
  Screen cur = view_.screen == Screen::QrFull ? Screen::Card
               : view_.screen == Screen::ProjectQr ? Screen::Projects : view_.screen;
  int idx = -1;
  for (int i = 0; i < n; ++i)
    if (cycle[i] == cur) idx = i;
  switch (s) {
    case Swipe::Right: return go(cycle[idx < 0 ? 0 : (idx + 1) % n]);
    case Swipe::Left: return go(cycle[idx < 0 ? 0 : (idx + n - 1) % n]);
    case Swipe::Up:
      if (view_.screen == Screen::Card && cfg_.qr_configured) return go(Screen::QrFull);
      return go(Screen::Card);
    case Swipe::Down: return go(Screen::Badge);
    default: return kActNone;
  }
}

uint32_t App::on_config_changed(const AppConfig &cfg) {
  const bool leaving_safe = cfg_.safe_mode && !cfg.safe_mode;
  cfg_ = cfg;
  view_.layout = uint8_t(cfg.layout ^ (layout_toggled_ ? 1 : 0));
  if (view_.project >= cfg.project_count) view_.project = 0;
  if (view_.screen == Screen::Projects && cfg.project_count == 0) view_.screen = Screen::Badge;
  if (view_.screen == Screen::QrFull && !cfg.qr_configured) view_.screen = Screen::Card;
  if (view_.screen == Screen::ProjectQr && !project_has_url(view_.project))
    view_.screen = cfg.project_count ? Screen::Projects : Screen::Badge;
  if (leaving_safe && view_.screen == Screen::Recovery) view_.screen = Screen::Badge;
  return kActRedraw;  // content may have changed; unchanged frames are suppressed by hash
}

uint32_t App::on_activity(uint32_t now_ms) {
  last_activity_ms_ = now_ms;
  return kActNone;
}

uint32_t App::on_tick(uint32_t now_ms, bool on_battery, bool display_settled) {
  uint32_t a = kActNone;
  if (gesture_on_ && cfg_.gesture_timeout_s && !sleep_pending_ &&
      now_ms - last_activity_ms_ >= uint32_t(cfg_.gesture_timeout_s) * 1000u) {
    a |= set_gesture_mode(false);  // stop the sensor and its IR emitter when idle
  }
  return a | on_tick_power(now_ms, on_battery, display_settled);
}

uint32_t App::on_tick_power(uint32_t now_ms, bool on_battery, bool display_settled) {
  if (sleep_pending_ || !on_battery || cfg_.sleep_timeout_s == 0 || cfg_.safe_mode) return kActNone;
  if (!display_settled) return kActNone;  // never cut power mid-refresh
  if (now_ms - last_activity_ms_ >= uint32_t(cfg_.sleep_timeout_s) * 1000u) {
    sleep_pending_ = true;
    return kActSleep;
  }
  return kActNone;
}

View App::sleep_view() const {
  View v = view_;
  if (cfg_.sleep_to_badge && v.screen != Screen::Recovery) v.screen = Screen::Badge;
  return v;
}

}  // namespace badge
