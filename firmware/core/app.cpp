#include "app.hpp"

#include <cstring>

namespace badge {

namespace {
const char *const kScreenNames[] = {"badge", "card", "projects", "qr", "info", "recovery"};
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
        return kActNone;
      default: return kActNone;
    }
  }
  switch (e.button) {
    case Button::A: return kActRedraw | kActCleanRefresh;
    case Button::B: return cfg_.qr_configured ? go(Screen::QrFull) : go(Screen::Card);
    case Button::C: return go(Screen::Info);
    case Button::Up:
      layout_toggled_ = !layout_toggled_;
      view_.layout = uint8_t(cfg_.layout ^ (layout_toggled_ ? 1 : 0));
      return (view_.screen == Screen::Badge) ? kActRedraw : kActNone;
    case Button::Down:
      sleep_pending_ = true;
      return kActSleep;
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
  return a | go(s);
}

uint32_t App::on_project_step(int delta) {
  const int n = cfg_.project_count;
  if (n <= 1) return kActNone;
  view_.project = uint8_t(((int(view_.project) + delta) % n + n) % n);
  return view_.screen == Screen::Projects ? kActRedraw : kActNone;
}

uint32_t App::on_config_changed(const AppConfig &cfg) {
  const bool leaving_safe = cfg_.safe_mode && !cfg.safe_mode;
  cfg_ = cfg;
  view_.layout = uint8_t(cfg.layout ^ (layout_toggled_ ? 1 : 0));
  if (view_.project >= cfg.project_count) view_.project = 0;
  if (view_.screen == Screen::Projects && cfg.project_count == 0) view_.screen = Screen::Badge;
  if (view_.screen == Screen::QrFull && !cfg.qr_configured) view_.screen = Screen::Card;
  if (leaving_safe && view_.screen == Screen::Recovery) view_.screen = Screen::Badge;
  return kActRedraw;  // content may have changed; unchanged frames are suppressed by hash
}

uint32_t App::on_activity(uint32_t now_ms) {
  last_activity_ms_ = now_ms;
  return kActNone;
}

uint32_t App::on_tick(uint32_t now_ms, bool on_battery, bool display_settled) {
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
