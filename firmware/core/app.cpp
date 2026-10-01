#include "app.hpp"

#include <cstring>

namespace badge {

namespace {
const char *const kScreenNames[] = {"badge", "card", "projects", "qr", "info", "recovery", "project-qr", "index"};
}

int index_viewport_top(int top, int sel, int count, int rows) {
  if (count <= rows || rows <= 0) return 0;
  if (sel < 0) sel = 0;
  if (sel >= count) sel = count - 1;
  if (sel < top) top = sel;
  if (sel >= top + rows) top = sel - rows + 1;
  if (top > count - rows) top = count - rows;
  return top < 0 ? 0 : top;
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
  click_.reset();
  c_frozen_ = false;
  nav_fresh_[0] = nav_fresh_[1] = false;
  index_sel_ = 0;
  index_return_ = Screen::Badge;
  index_dirty_ = false;
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
  if (view_.screen == Screen::Index) index_dirty_ = false;  // leaving: nothing left to draw
  view_.screen = s;
  return kActRedraw;
}

// A screen restored or kept across a content change must still be valid.
Screen App::sanitized(Screen s) const {
  if (s == Screen::Recovery && !cfg_.safe_mode) return Screen::Badge;
  if (s == Screen::Index) return Screen::Badge;
  if (s == Screen::QrFull && !cfg_.qr_configured) return Screen::Card;
  if (s == Screen::ProjectQr && !project_has_url(view_.project)) s = Screen::Projects;
  if (s == Screen::Projects && cfg_.project_count == 0) return Screen::Badge;
  return s;
}

uint32_t App::open_project(int n) {
  if (cfg_.project_count == 0) return kActNone;
  if (n < 0 || n >= cfg_.project_count) n = 0;
  uint32_t a = kActNone;
  if (view_.project != n) {
    view_.project = uint8_t(n);
    a = kActRedraw;
  }
  return a | go(Screen::Projects);
}

uint32_t App::open_index(int sel) {
  const int n = cfg_.project_count;
  if (view_.screen != Screen::Index) index_return_ = view_.screen;
  index_sel_ = uint8_t(n > 0 && sel >= 0 && sel < n ? sel : 0);
  index_dirty_ = false;
  nav_fresh_[0] = nav_fresh_[1] = false;  // UP/DOWN holds from before do not scroll it
  view_.index_sel = index_sel_;
  view_.index_top = uint8_t(index_viewport_top(view_.index_top, index_sel_, n));
  view_.screen = Screen::Index;
  return kActRedraw;
}

void App::index_step(int delta, bool wrap) {
  const int n = cfg_.project_count;
  if (n <= 1) return;
  int s = int(index_sel_) + delta;
  if (wrap) s = (s % n + n) % n;
  else s = s < 0 ? 0 : s >= n ? n - 1 : s;
  index_sel_ = uint8_t(s);
  index_dirty_ = true;
}

// Show the live highlight. Unchanged (e.g. DOWN then UP): no frame at all.
uint32_t App::publish_index() {
  index_dirty_ = false;
  if (view_.index_sel == index_sel_) return kActNone;
  view_.index_sel = index_sel_;
  view_.index_top = uint8_t(index_viewport_top(view_.index_top, index_sel_, cfg_.project_count));
  return kActRedraw;
}

uint32_t App::on_click(Click c) {
  const Screen cur = view_.screen;
  if (c == Click::None) return kActNone;
  if (cur == Screen::Recovery) return c == Click::Long ? go(Screen::Info) : kActNone;
  switch (c) {
    case Click::Single:
      // Inside the index: confirm. Only now does the remembered project change.
      return open_project(cur == Screen::Index ? index_sel_ : view_.project);
    case Click::Double:
      return cur == Screen::Index ? kActNone : open_index(view_.project);
    case Click::Long:
      return open_project(0);
    default: return kActNone;
  }
}

uint32_t App::power_off() {
  sleep_pending_ = true;
  click_.cancel();
  c_frozen_ = false;
  index_dirty_ = false;  // nothing queued may be drawn after this
  return kActSleep | (gesture_on_ ? kActGestureMode : kActNone);
}

uint32_t App::on_index_button(const ButtonEvent &e) {
  switch (e.button) {
    case Button::Up:
    case Button::Down: {
      bool &fresh = nav_fresh_[e.button == Button::Up ? 0 : 1];
      if (e.gesture == Gesture::Press) fresh = true;
      if (!fresh) return kActNone;  // a hold that began before the index opened
      if (e.gesture == Gesture::Short) fresh = false;
      // DOWN held for kHoldPowerOffMs: the usual power-off sequence. The
      // highlight is not confirmed and nothing queued is drawn.
      if (e.gesture == Gesture::Hold) return power_off();
      // Any other UP/DOWN activity (press, repeat, release) postpones the redraw.
      index_quiet_until_ = e.t_ms + kIndexSettleMs;
      const int d = e.button == Button::Up ? -1 : 1;
      if (e.gesture == Gesture::Repeat) index_step(d, false);  // blind hold: stop at the ends
      else if (e.gesture == Gesture::Short && e.count == 0) index_step(d, true);  // a tap
      // Long (1 s): taken by scrolling here, so no gesture toggle / power-off.
      return kActNone;
    }
    case Button::A:
      if (e.gesture == Gesture::Short) return go(sanitized(index_return_));  // cancel; project kept
      if (e.gesture == Gesture::Long) return publish_index() | kActRedraw | kActCleanRefresh;
      return kActNone;
    default: return kActNone;
  }
}

// What a pending single C does when another button's gesture resolves before
// the double-press window has passed (see app.hpp).
bool App::pending_c_applies_first(const ButtonEvent &e) {
  if (e.gesture == Gesture::Long) return !(e.button == Button::Down);  // DOWN long: power off
  if (e.gesture == Gesture::Short) return e.button == Button::Up || e.button == Button::Down;
  return false;
}

uint32_t App::poll_click(uint32_t now_ms) {
  if (c_frozen_ && !click_.pending()) c_frozen_ = false;
  if (c_frozen_) {
    // Another button is down: the C waits for its outcome. If that never
    // arrives (lost events), drop the C rather than fire it late.
    if (int32_t(now_ms - c_frozen_until_) >= 0) {
      c_frozen_ = false;
      click_.cancel();
    }
    return kActNone;
  }
  return on_click(click_.poll(now_ms));
}

uint32_t App::on_button(const ButtonEvent &e) {
  last_activity_ms_ = e.t_ms;
  if (sleep_pending_) return kActNone;  // committed to powering off
  uint32_t a = poll_click(e.t_ms);  // an expired single C acts first
  if (e.button == Button::C) {
    // C itself decides: a second press inside the window is a double, a
    // later one starts a new interaction (the frozen single is dropped).
    c_frozen_ = false;
    return a | on_click(click_.on_event(e.gesture, e.t_ms, view_.screen == Screen::Index));
  }
  // Another button while a single C waits for its window: freeze the window
  // on its press, then let its gesture decide (pending_c_applies_first).
  if (click_.pending()) {
    if (e.gesture == Gesture::Press && !c_frozen_) {
      c_frozen_ = true;
      c_frozen_until_ = e.t_ms + kLongPressMs + kPendingCSlackMs;
    } else if (e.gesture == Gesture::Short || e.gesture == Gesture::Long) {
      c_frozen_ = false;
      if (pending_c_applies_first(e)) a |= on_click(click_.flush());
      else click_.cancel();  // superseded: the C is never drawn
    }
  }
  if (view_.screen == Screen::Recovery) {
    // Safe mode stays up until fixed over USB and rebooted; only allow
    // viewing diagnostics.
    if (e.button == Button::User && e.gesture == Gesture::Short) return a | go(Screen::Info);
    return a;
  }
  if (view_.screen == Screen::Index) return a | on_index_button(e);
  if (e.gesture == Gesture::Press || e.gesture == Gesture::Repeat || e.gesture == Gesture::Hold) return a;
  if (e.gesture == Gesture::Short) {
    switch (e.button) {
      case Button::A: return a | go(Screen::Badge);
      case Button::B: return a | go(Screen::Card);
      case Button::User: return a | go(Screen::Info);
      case Button::Up:
      case Button::Down:
        if (view_.screen == Screen::Projects) return a | on_project_step(e.button == Button::Up ? -1 : 1);
        if (view_.screen == Screen::QrFull) return a | go(Screen::Card);
        if (view_.screen == Screen::ProjectQr) return a | go(Screen::Projects);  // same project
        return a;
      default: return a;
    }
  }
  switch (e.button) {
    case Button::A: return a | kActRedraw | kActCleanRefresh;
    case Button::B:
      // Project context: the project's own repository QR, never the contact one.
      if (view_.screen == Screen::ProjectQr) return a | go(Screen::Projects);
      if (view_.screen == Screen::Projects) return a | (project_has_url(view_.project) ? go(Screen::ProjectQr) : kActNone);
      return a | (cfg_.qr_configured ? go(Screen::QrFull) : go(Screen::Card));
    case Button::Up:
      return a | set_gesture_mode(!gesture_on_);
    case Button::Down:
      return a | power_off();
    case Button::User:
      layout_toggled_ = !layout_toggled_;
      view_.layout = uint8_t(cfg_.layout ^ (layout_toggled_ ? 1 : 0));
      return a | ((view_.screen == Screen::Badge) ? kActRedraw : kActNone);
    default: return a;
  }
}

uint32_t App::on_poll(uint32_t now_ms) {
  if (sleep_pending_) {
    click_.cancel();
    return kActNone;
  }
  uint32_t a = poll_click(now_ms);
  if (view_.screen == Screen::Index && index_dirty_ && int32_t(now_ms - index_quiet_until_) >= 0) a |= publish_index();
  return a;
}

uint32_t App::on_screen_request(Screen s, int project) {
  if (s == Screen::Recovery && !cfg_.safe_mode) return kActNone;
  click_.cancel();  // a USB request changes the context: no late C action
  c_frozen_ = false;
  if (s == Screen::Index) {
    if (view_.screen != Screen::Index) return open_index(project >= 0 ? project : view_.project);
    if (project >= 0 && project < cfg_.project_count) {
      index_sel_ = uint8_t(project);
      return publish_index();
    }
    return kActNone;
  }
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
  if (view_.screen == Screen::Index) {  // CLI 'project next|prev' in the index: move the highlight
    click_.cancel();
    index_step(delta, true);
    return publish_index();
  }
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
  if (view_.screen == Screen::Index) return kActNone;  // the index is modal: a stray wave never leaves it
  // A swipe picks a screen of its own: a pending single C is dropped.
  click_.cancel();
  c_frozen_ = false;
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
  click_.cancel();
  c_frozen_ = false;
  view_.layout = uint8_t(cfg.layout ^ (layout_toggled_ ? 1 : 0));
  if (view_.project >= cfg.project_count) view_.project = 0;
  if (view_.screen == Screen::Projects && cfg.project_count == 0) view_.screen = Screen::Badge;
  if (view_.screen == Screen::QrFull && !cfg.qr_configured) view_.screen = Screen::Card;
  if (view_.screen == Screen::ProjectQr && !project_has_url(view_.project))
    view_.screen = cfg.project_count ? Screen::Projects : Screen::Badge;
  if (leaving_safe && view_.screen == Screen::Recovery) view_.screen = Screen::Badge;
  if (index_sel_ >= cfg.project_count) index_sel_ = 0;
  if (view_.index_sel >= cfg.project_count) view_.index_sel = index_sel_;
  view_.index_top = uint8_t(index_viewport_top(view_.index_top, view_.index_sel, cfg.project_count));
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
    click_.cancel();
    c_frozen_ = false;
    index_dirty_ = false;
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
