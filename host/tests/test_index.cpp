// Project index, quiet browsing and C single/double/long handling: App
// state machine, index rendering, and the whole input -> app -> render ->
// display pipeline driven by raw button samples.
#include <atomic>
#include <string>
#include <vector>

#include "app.hpp"
#include "check.hpp"
#include "display_pipeline.hpp"
#include "renderer.hpp"
#include "sim_panel.hpp"
#include "text.hpp"

using namespace badge;

namespace {

AppConfig icfg(int projects, uint16_t url_mask = 0x0FFF, bool qr = true) {
  AppConfig c;
  c.project_count = uint8_t(projects);
  c.project_url_mask = uint16_t(url_mask & ((1u << projects) - 1));
  c.qr_configured = qr;
  c.sleep_timeout_s = 60;
  return c;
}

// Drives the App with gestures as the ButtonTracker reports them, on a
// clock that only moves forward, calling on_poll like the main loop.
struct Drive {
  App a;
  uint32_t t = 1000;
  std::vector<View> redraws;  // view after every call that asked for a redraw
  uint32_t acts = 0;          // OR of every action since the last take()
  void note(uint32_t r) {
    acts |= r;
    if (r & kActRedraw) redraws.push_back(a.view());
  }
  void boot(const AppConfig &c) {
    a.boot(c, -1, t);
    redraws.clear();
    acts = 0;
  }
  void ev(Button b, Gesture g, uint8_t count = 0) { note(a.on_button({b, g, t, count})); }
  void wait(uint32_t ms) {  // main loop passes every 5 ms
    for (uint32_t i = 0; i < ms; i += 5) {
      t += 5;
      note(a.on_poll(t));
    }
  }
  void tap(Button b, uint32_t hold = 80) {
    ev(b, Gesture::Press);
    wait(hold);
    ev(b, Gesture::Short);
  }
  void tap_c() {  // single C, then let the double-press window expire
    tap(Button::C);
    wait(kDoublePressMs + 5);
  }
  void double_c() {
    tap(Button::C);
    wait(150);
    tap(Button::C);
  }
  void hold(Button b) {
    ev(b, Gesture::Press);
    wait(kLongPressMs);
    ev(b, Gesture::Long);
  }
  // Hold UP/DOWN for `ms`, with repeats as the tracker would produce them.
  void hold_nav(Button b, uint32_t ms) {
    ev(b, Gesture::Press);
    uint32_t held = 0, next = kRepeatDelayMs;
    uint8_t reps = 0;
    while (held + 5 <= ms) {
      wait(5);
      held += 5;
      if (held == next) {
        ev(b, Gesture::Repeat, ++reps);
        next += kRepeatIntervalMs;
      }
      if (held == kLongPressMs) ev(b, Gesture::Long);
      if (held == kHoldPowerOffMs && (kHoldMask & (1u << int(b)))) ev(b, Gesture::Hold);
    }
    if (held < kLongPressMs) ev(b, Gesture::Short, reps);
  }
  uint32_t take() {
    const uint32_t r = acts;
    acts = 0;
    return r;
  }
};

}  // namespace

// ------------------------------------------------------------ C gestures

TEST(index_short_c_is_deferred_and_opens_last_project) {
  Drive d;
  d.boot(icfg(7));
  d.a.on_screen_request(Screen::Projects, 3);
  d.a.on_button({Button::A, Gesture::Short, d.t});
  d.redraws.clear();
  d.tap(Button::C);
  CHECK(d.a.click_pending());
  d.wait(kDoublePressMs - 100);
  CHECK(d.a.view().screen == Screen::Badge);  // still waiting for a possible second press
  CHECK(d.redraws.empty());
  d.wait(200);
  CHECK(d.a.view().screen == Screen::Projects);
  CHECK_EQ(d.a.view().project, 3);
  CHECK_EQ(d.redraws.size(), 1u);  // exactly one action
  d.wait(5000);
  CHECK_EQ(d.redraws.size(), 1u);
}

TEST(index_double_c_opens_index_without_a_project_first) {
  Drive d;
  d.boot(icfg(7));
  d.a.on_screen_request(Screen::Projects, 4);
  d.a.on_button({Button::B, Gesture::Short, d.t});  // on the card, project 5 remembered
  d.redraws.clear();
  d.double_c();
  CHECK(d.a.view().screen == Screen::Index);
  CHECK_EQ(d.a.view().index_sel, 4);  // highlights the last-viewed project
  CHECK_EQ(d.a.index_selection(), 4);
  CHECK_EQ(d.redraws.size(), 1u);
  CHECK(d.redraws[0].screen == Screen::Index);  // never the project page in between
  d.wait(2000);
  CHECK_EQ(d.redraws.size(), 1u);
  CHECK(!d.a.click_pending());
  // Double C inside the index: harmless (first tap confirms, second reopens
  // the same project page).
  d.boot(icfg(7));
  d.double_c();
  d.redraws.clear();
  d.double_c();
  d.wait(1000);
  CHECK(d.a.view().screen == Screen::Projects);
  CHECK_EQ(d.a.view().project, 0);
  CHECK_EQ(d.redraws.size(), 1u);
}

TEST(index_long_c_jumps_to_project_one_everywhere) {
  Drive d;
  d.boot(icfg(7));
  // From a middle project.
  d.a.on_screen_request(Screen::Projects, 3);
  d.hold(Button::C);
  CHECK(d.a.view().screen == Screen::Projects);
  CHECK_EQ(d.a.view().project, 0);
  // From a project QR.
  d.a.on_screen_request(Screen::ProjectQr, 4);
  CHECK(d.a.view().screen == Screen::ProjectQr);
  d.redraws.clear();
  d.hold(Button::C);
  CHECK(d.a.view().screen == Screen::Projects);
  CHECK_EQ(d.a.view().project, 0);
  CHECK_EQ(d.redraws.size(), 1u);
  d.wait(2000);  // the release after a long press does nothing
  CHECK_EQ(d.redraws.size(), 1u);
  // From the contact QR, the badge and diagnostics.
  for (Screen s : {Screen::QrFull, Screen::Badge, Screen::Info}) {
    d.a.on_screen_request(Screen::Projects, 5);
    d.a.on_screen_request(s);
    d.hold(Button::C);
    CHECK(d.a.view().screen == Screen::Projects && d.a.view().project == 0);
  }
  // Already on project 1: nothing to redraw.
  d.redraws.clear();
  d.hold(Button::C);
  CHECK(d.redraws.empty());
  // From inside the index, also project 1 (and it becomes the remembered one).
  d.a.on_screen_request(Screen::Projects, 6);
  d.double_c();
  d.tap(Button::Up);
  d.hold(Button::C);
  CHECK(d.a.view().screen == Screen::Projects && d.a.view().project == 0);
  // Long C on the second press of a double attempt: project 1, no index.
  d.a.on_screen_request(Screen::Projects, 6);
  d.a.on_screen_request(Screen::Card);
  d.redraws.clear();
  d.tap(Button::C);
  d.wait(100);
  d.hold(Button::C);
  d.wait(1000);
  CHECK_EQ(d.redraws.size(), 1u);
  CHECK(d.a.view().screen == Screen::Projects && d.a.view().project == 0);
}

TEST(index_no_projects) {
  Drive d;
  d.boot(icfg(0));
  d.tap_c();
  CHECK(d.a.view().screen == Screen::Badge);  // short C: nothing to open
  d.hold(Button::C);
  CHECK(d.a.view().screen == Screen::Badge);
  d.double_c();
  CHECK(d.a.view().screen == Screen::Index);  // shows "No projects configured"
  d.tap(Button::Down);
  d.wait(1000);
  d.tap(Button::C);
  CHECK(d.a.view().screen == Screen::Index);  // nothing to confirm
  d.tap(Button::A);
  CHECK(d.a.view().screen == Screen::Badge);
}

// --------------------------------------------------------- quiet browsing

TEST(index_selection_moves_in_ram_and_redraws_after_quiet) {
  Drive d;
  d.boot(icfg(7));
  d.double_c();
  d.redraws.clear();
  d.tap(Button::Down);
  CHECK_EQ(d.a.index_selection(), 1);  // immediately, in RAM
  CHECK_EQ(d.a.view().index_sel, 0);   // not drawn yet
  CHECK(d.a.index_redraw_pending());
  d.wait(kIndexSettleMs - 10);
  CHECK(d.redraws.empty());
  d.wait(20);
  CHECK_EQ(d.redraws.size(), 1u);
  CHECK_EQ(d.redraws[0].index_sel, 1);
  CHECK_EQ(d.a.view().project, 0);  // the remembered project is untouched
  // Rapid taps coalesce into one redraw with the newest highlight.
  d.redraws.clear();
  for (int i = 0; i < 4; ++i) {
    d.tap(Button::Down, 60);
    d.wait(60);
  }
  d.wait(kIndexSettleMs + 10);
  CHECK_EQ(d.redraws.size(), 1u);
  CHECK_EQ(d.redraws[0].index_sel, 5);
  // Taps wrap at the ends.
  d.tap(Button::Down);
  d.tap(Button::Down);
  d.wait(kIndexSettleMs + 10);
  CHECK_EQ(d.a.view().index_sel, 0);
  d.tap(Button::Up);
  d.wait(kIndexSettleMs + 10);
  CHECK_EQ(d.a.view().index_sel, 6);
}

TEST(index_hold_repeats_without_redraws_and_stops_at_the_end) {
  Drive d;
  d.boot(icfg(12));
  d.double_c();
  d.redraws.clear();
  d.hold_nav(Button::Down, kRepeatDelayMs + 3 * kRepeatIntervalMs);  // 4 repeats, short hold
  CHECK_EQ(d.a.index_selection(), 4);  // the release adds no extra step
  CHECK(d.redraws.empty());            // nothing drawn during the hold
  d.wait(kIndexSettleMs + 10);
  CHECK_EQ(d.redraws.size(), 1u);
  CHECK_EQ(d.redraws[0].index_sel, 4);
  // A long hold runs to the last entry and stops there (no wrap, no power
  // off at the long-press threshold, no redraw while held). Power-off needs
  // kHoldPowerOffMs (index_down_hold_powers_off).
  d.redraws.clear();
  d.hold_nav(Button::Down, kHoldPowerOffMs - 5);
  CHECK_EQ(d.a.index_selection(), 11);
  CHECK(!d.a.sleeping());
  CHECK(d.redraws.empty());
  d.wait(kIndexSettleMs + 10);  // released after the long press: no event, the quiet timer still fires
  CHECK_EQ(d.redraws.size(), 1u);
  CHECK_EQ(d.a.view().index_sel, 11);
  CHECK(d.a.view().index_top == 12 - kIndexRows);
  // Holding UP does not toggle gesture mode.
  d.hold_nav(Button::Up, 4000);
  CHECK(!d.a.gesture_mode());
  CHECK_EQ(d.a.index_selection(), 0);
  d.wait(kIndexSettleMs + 10);
  CHECK_EQ(d.a.view().index_top, 0);
}

TEST(index_unchanged_selection_draws_nothing) {
  Drive d;
  d.boot(icfg(7));
  d.double_c();
  d.redraws.clear();
  d.tap(Button::Down);
  d.wait(50);
  d.tap(Button::Up);
  d.wait(1000);
  CHECK(d.redraws.empty());
  CHECK(!d.a.index_redraw_pending());
  // One entry: nothing moves at all.
  d.boot(icfg(1));
  d.double_c();
  d.redraws.clear();
  d.tap(Button::Down);
  d.hold_nav(Button::Up, 2000);
  d.wait(1000);
  CHECK(d.redraws.empty());
  CHECK_EQ(d.a.index_selection(), 0);
}

TEST(index_confirm_versus_cancel) {
  Drive d;
  d.boot(icfg(7));
  d.a.on_screen_request(Screen::Projects, 2);
  // Cancel: back to the project page, remembered project kept, and the
  // queued highlight is never drawn.
  d.double_c();
  d.tap(Button::Down);
  d.tap(Button::Down);
  d.redraws.clear();
  d.tap(Button::A);
  CHECK(d.a.view().screen == Screen::Projects);
  CHECK_EQ(d.a.view().project, 2);
  d.wait(2000);
  CHECK_EQ(d.redraws.size(), 1u);
  CHECK(d.redraws[0].screen == Screen::Projects && d.redraws[0].project == 2);
  d.tap_c();  // short C still opens the remembered project
  CHECK_EQ(d.a.view().project, 2);
  // Reopening highlights the remembered project, not the abandoned candidate.
  d.double_c();
  CHECK_EQ(d.a.view().index_sel, 2);
  // Confirm opens at once (no double-press wait) and remembers.
  d.tap(Button::Down);
  d.tap(Button::Down);
  d.tap(Button::Down);
  d.redraws.clear();
  d.tap(Button::C);
  CHECK(d.a.view().screen == Screen::Projects);
  CHECK_EQ(d.a.view().project, 5);
  CHECK_EQ(d.redraws.size(), 1u);
  CHECK(d.redraws[0].screen == Screen::Projects && d.redraws[0].project == 5);  // only the final project
  d.wait(2000);
  CHECK_EQ(d.redraws.size(), 1u);
  d.a.on_button({Button::B, Gesture::Short, d.t});
  d.tap_c();
  CHECK_EQ(d.a.view().project, 5);
  // Cancel restores a project QR context exactly.
  d.a.on_screen_request(Screen::ProjectQr, 1);
  d.double_c();
  CHECK_EQ(d.a.view().index_sel, 1);
  d.tap(Button::Up);
  d.tap(Button::A);
  CHECK(d.a.view().screen == Screen::ProjectQr);
  CHECK_EQ(d.a.view().project, 1);
  // ... and the contact QR / card / badge / diagnostics.
  for (Screen s : {Screen::QrFull, Screen::Card, Screen::Badge, Screen::Info}) {
    d.a.on_screen_request(s);
    d.double_c();
    d.tap(Button::Down);
    d.tap(Button::A);
    CHECK(d.a.view().screen == s);
    CHECK_EQ(d.a.view().project, 1);
  }
}

TEST(index_ignores_other_controls_and_swipes) {
  Drive d;
  AppConfig c = icfg(7);
  d.boot(c);
  d.a.set_gesture_mode(true);
  d.double_c();
  const uint8_t layout = d.a.view().layout;
  d.tap(Button::B);
  d.hold(Button::B);
  d.tap(Button::User);
  d.hold(Button::User);
  CHECK(d.a.on_swipe(Swipe::Right, d.t) == kActNone);
  CHECK(d.a.on_swipe(Swipe::Down, d.t) == kActNone);
  CHECK(d.a.view().screen == Screen::Index);
  CHECK_EQ(d.a.view().layout, layout);
  // A long: clean refresh of the index (with the newest highlight).
  d.tap(Button::Down);
  d.take();
  d.hold(Button::A);
  CHECK((d.take() & (kActRedraw | kActCleanRefresh)) == (kActRedraw | kActCleanRefresh));
  CHECK_EQ(d.a.view().index_sel, 1);
  CHECK(d.a.view().screen == Screen::Index);
}

TEST(index_viewport_keeps_selection_visible) {
  for (int n : {0, 1, 6, 7, 8, 12}) {
    int top = 0;
    // Down through the list, then back up: always visible, minimal scrolling.
    for (int pass = 0; pass < 2; ++pass)
      for (int k = 0; k < n; ++k) {
        const int sel = pass == 0 ? k : n - 1 - k;
        const int prev = top;
        top = index_viewport_top(top, sel, n);
        CHECK(top >= 0);
        CHECK(n <= kIndexRows ? top == 0 : top <= n - kIndexRows);
        CHECK(sel >= top && sel < top + kIndexRows);
        if (prev <= sel && sel < prev + kIndexRows) CHECK_EQ(top, prev);  // no needless scroll
      }
  }
  // Out-of-range input is clamped.
  CHECK_EQ(index_viewport_top(50, 3, 12), 3);
  CHECK_EQ(index_viewport_top(-4, 0, 12), 0);
  CHECK_EQ(index_viewport_top(0, 40, 12), 12 - kIndexRows);
  // Wrapping from the last entry back to the first jumps the window to the top.
  CHECK_EQ(index_viewport_top(12 - kIndexRows, 0, 12), 0);
}

// ------------------------------------------------------- context changes

TEST(index_pending_single_c_and_context_changes) {
  Drive d;
  d.boot(icfg(7));
  d.a.on_screen_request(Screen::Projects, 3);
  d.a.on_screen_request(Screen::Card);
  // USB screen change while a single C waits: it never fires later.
  d.tap(Button::C);
  d.a.on_screen_request(Screen::Badge);
  d.wait(2000);
  CHECK(d.a.view().screen == Screen::Badge);
  // A content change over USB: same.
  d.tap(Button::C);
  d.a.on_config_changed(icfg(7));
  d.wait(2000);
  CHECK(d.a.view().screen == Screen::Badge);
  // A press that began before a context change is ignored on release.
  d.ev(Button::C, Gesture::Press);
  d.a.on_screen_request(Screen::Card);
  d.wait(80);
  d.ev(Button::C, Gesture::Short);
  d.wait(2000);
  CHECK(d.a.view().screen == Screen::Card);
  // Long B while C waits: the C applies first, then B acts on the project
  // page it opened (that project's QR, not the contact QR). See also
  // pending_c_* below.
  d.tap(Button::C);
  d.wait(100);
  d.hold(Button::B);
  CHECK(d.a.view().screen == Screen::ProjectQr);
  CHECK_EQ(d.a.view().project, 3);
  d.wait(2000);
  CHECK(d.a.view().screen == Screen::ProjectQr);  // nothing fires late
  // ... and a short A right after a C tap ends on the badge, with one action each.
  d.redraws.clear();
  d.tap(Button::C);
  d.tap(Button::A);
  d.wait(2000);
  CHECK(d.a.view().screen == Screen::Badge);
  // Power-off while a single C waits: nothing happens after it.
  d.tap(Button::C);
  d.a.on_tick(d.t + 61000, true, true);
  CHECK(d.a.sleeping());
  d.redraws.clear();
  d.wait(2000);
  CHECK(d.redraws.empty());
  CHECK(d.a.view().screen == Screen::Badge);
  // Boot (wake) starts clean: a release from before the reset is not a press.
  d.boot(icfg(7));
  d.ev(Button::C, Gesture::Short);
  d.wait(2000);
  CHECK(d.a.view().screen == Screen::Badge);
}

TEST(index_content_change_while_browsing) {
  Drive d;
  d.boot(icfg(12));
  d.a.on_screen_request(Screen::Projects, 2);
  d.double_c();
  for (int i = 0; i < 8; ++i) d.tap(Button::Down);
  CHECK_EQ(d.a.index_selection(), 10);
  d.a.on_config_changed(icfg(4));  // list shrank over USB
  CHECK(d.a.view().screen == Screen::Index);
  CHECK(d.a.index_selection() < 4);
  CHECK(d.a.view().index_sel < 4);
  CHECK_EQ(d.a.view().index_top, 0);
  d.wait(1000);
  d.tap(Button::C);
  CHECK(d.a.view().project < 4);
  // Cancel to a project QR whose link was removed meanwhile: its page instead.
  d.a.on_screen_request(Screen::ProjectQr, 1);
  d.double_c();
  d.a.on_config_changed(icfg(4, 0x1));
  d.tap(Button::A);
  CHECK(d.a.view().screen == Screen::Projects);
  CHECK_EQ(d.a.view().project, 1);
  // All projects removed while in the index: the empty index, then back.
  d.double_c();
  d.a.on_config_changed(icfg(0));
  CHECK(d.a.view().screen == Screen::Index);
  d.tap(Button::A);
  CHECK(d.a.view().screen == Screen::Badge);
}

TEST(index_cli_requests) {
  Drive d;
  d.boot(icfg(7));
  d.a.on_screen_request(Screen::Card);
  CHECK_EQ(d.a.on_screen_request(Screen::Index, 4), kActRedraw);
  CHECK(d.a.view().screen == Screen::Index && d.a.view().index_sel == 4);
  CHECK_EQ(d.a.view().project, 0);  // only highlighted
  CHECK_EQ(d.a.on_project_step(1), kActRedraw);  // 'project next' moves the highlight
  CHECK_EQ(d.a.view().index_sel, 5);
  CHECK_EQ(d.a.on_screen_request(Screen::Index, 5), kActNone);  // unchanged
  CHECK_EQ(d.a.on_screen_request(Screen::Index, 6), kActRedraw);
  d.tap(Button::A);
  CHECK(d.a.view().screen == Screen::Card);  // the screen before the request
  CHECK(std::strcmp(screen_name(Screen::Index), "index") == 0);
  Screen s;
  CHECK(screen_from_name("index", &s) && s == Screen::Index);
}

TEST(index_sleep_and_safe_mode) {
  Drive d;
  AppConfig c = icfg(7);
  c.sleep_to_badge = true;
  d.boot(c);
  d.double_c();
  CHECK(d.a.sleep_view().screen == Screen::Badge);
  d.tap(Button::Down);
  d.a.on_tick(d.t + 61000, true, true);  // auto power-off with a highlight queued
  CHECK(d.a.sleeping());
  d.redraws.clear();
  d.wait(1000);
  CHECK(d.redraws.empty());  // no index redraw after committing to power off
  c.safe_mode = true;
  d.boot(c);
  d.double_c();
  d.tap_c();
  CHECK(d.a.view().screen == Screen::Recovery);
  CHECK_EQ(d.a.on_screen_request(Screen::Index), kActRedraw);  // USB may still show it
}

// ---------------------------------------------------------------- rendering

namespace {
Settings g_is;
Framebuffer g_ifb;
void iset(const char *k, const char *v) { CHECK(settings_set(&g_is, *find_field(k), v) == SetResult::Ok); }
void projects(int n, const char *title_unit = nullptr) {
  static const char *f[] = {"title", "tagline", "body", "link", "status", "banner"};
  for (int i = 1; i <= kMaxProjects; ++i)
    for (const char *k : f) iset(("project" + std::to_string(i) + "." + k).c_str(), "");
  for (int i = 1; i <= n; ++i) {
    std::string t = "Project " + std::to_string(i);
    if (title_unit) {
      t.clear();
      while (t.size() + std::strlen(title_unit) < find_field("project1.title")->size) t += title_unit;
    }
    iset(("project" + std::to_string(i) + ".title").c_str(), t.c_str());
  }
}
RenderContext ictx() {
  RenderContext c;
  c.settings = &g_is;
  return c;
}
bool white(const Framebuffer &fb, Rect r) {
  for (int x = r.x; x < r.right(); ++x)
    for (int y = r.y; y < r.bottom(); ++y)
      if (fb.get(x, y) == Ink::Black) return false;
  return true;
}
int black_count(const Framebuffer &fb, Rect r) {
  int n = 0;
  for (int x = r.x; x < r.right(); ++x)
    for (int y = r.y; y < r.bottom(); ++y) n += fb.get(x, y) == Ink::Black;
  return n;
}
}  // namespace

TEST(render_index_sample_portfolio) {
  settings_defaults(&g_is);
  RenderContext c = ictx();
  const int n = configured_project_count(g_is.profile);
  CHECK(n >= 7 && n <= kIndexRows);  // the sample fits without scrolling
  std::vector<uint32_t> seen;
  for (int s = 0; s < n; ++s) {
    View v;
    v.screen = Screen::Index;
    v.index_sel = uint8_t(s);
    render(g_ifb, v, c);
    const IndexGeometry g = index_geometry(v, c);
    CHECK(!g.scrollbar);
    CHECK_EQ(g.shown, n);
    CHECK_EQ(g.top, 0);
    // The highlight bar is solid black at its edges and inside the list.
    CHECK(g.selected.x >= g.list.x && g.selected.right() <= g.list.right());
    CHECK(g.selected.y >= g.list.y && g.selected.bottom() <= g.list.bottom());
    for (int y = g.selected.y; y < g.selected.bottom(); ++y) CHECK(g_ifb.get(g.selected.x, y) == Ink::Black);
    // Other rows are not highlighted.
    for (int r = 0; r < g.shown; ++r)
      if (r != s) CHECK(g_ifb.get(g.list.x, g.list.y + r * kIndexRowH + 1) == Ink::White);
    // Every name fits its row completely (no ellipsis).
    const int idx = nth_configured_project(g_is.profile, s);
    CHECK(text_width(fonts::sans_11, g_is.profile.projects[idx].title) <= g.list.right() - 4 - (g.list.x + 24));
    for (uint32_t h : seen) CHECK(h != g_ifb.hash());
    seen.push_back(g_ifb.hash());
  }
  // Configured order: the last entry last, the teaser sixth.
  CHECK_STR(g_is.profile.projects[nth_configured_project(g_is.profile, n - 1)].title, "Weather Station");
  CHECK_STR(g_is.profile.projects[nth_configured_project(g_is.profile, 5)].title, "Secret Project");
}

TEST(render_index_empty_one_and_twelve) {
  settings_defaults(&g_is);
  RenderContext c = ictx();
  View v;
  v.screen = Screen::Index;
  // Empty: a message, no highlight bar.
  projects(0);
  render(g_ifb, v, c);
  IndexGeometry g = index_geometry(v, c);
  CHECK_EQ(g.count, 0);
  CHECK(g.selected.empty());
  CHECK(black_count(g_ifb, {16, 50, 296 - 32, 20}) > 50);
  // One: highlighted, no scrollbar, no UP/DOWN triangles.
  projects(1);
  render(g_ifb, v, c);
  g = index_geometry(v, c);
  CHECK_EQ(g.shown, 1);
  CHECK(!g.scrollbar);
  CHECK(white(g_ifb, {296 - 14, 16, 12, 8}));
  CHECK(white(g_ifb, {296 - 14, 116, 12, 4}));
  CHECK(g_ifb.get(g.selected.x, g.selected.y + 1) == Ink::Black);
  // Twelve: scroll as DOWN would; the highlight is always in view, the thumb
  // in its track, and the window moves only at the edges.
  projects(12);
  int top = 0;
  std::vector<int> thumbs;
  for (int s = 0; s < 12; ++s) {
    v.index_sel = uint8_t(s);
    v.index_top = uint8_t(top = index_viewport_top(top, s, 12));
    render(g_ifb, v, c);
    g = index_geometry(v, c);
    CHECK(g.scrollbar);
    CHECK_EQ(g.top, top);
    CHECK_EQ(g.shown, kIndexRows);
    CHECK(g.sel >= g.top && g.sel < g.top + kIndexRows);
    CHECK(g.selected.y >= g.list.y && g.selected.bottom() <= g.list.bottom());
    CHECK(g.thumb.y >= g.track.y && g.thumb.bottom() <= g.track.bottom());
    CHECK(g_ifb.get(g.thumb.x + 1, g.thumb.y + 1) == Ink::Black);
    thumbs.push_back(g.thumb.y);
  }
  CHECK_EQ(thumbs.front(), g.track.y);
  CHECK_EQ(thumbs.back(), g.track.bottom() - g.thumb.h);
  // Out-of-range view fields never read past the table.
  v.index_sel = 200;
  v.index_top = 200;
  render(g_ifb, v, c);
  CHECK_EQ(index_geometry(v, c).sel, 0);
}

TEST(render_index_long_names_stay_in_their_rows) {
  settings_defaults(&g_is);
  projects(12, "W");
  RenderContext c = ictx();
  View v;
  v.screen = Screen::Index;
  for (int s : {0, 6, 11}) {
    v.index_sel = uint8_t(s);
    v.index_top = uint8_t(index_viewport_top(0, s, 12));
    render(g_ifb, v, c);
    const IndexGeometry g = index_geometry(v, c);
    // Nothing between the list and the scrollbar track, nor left of the list.
    CHECK(white(g_ifb, {g.list.right(), g.list.y, int16_t(g.track.x - g.list.right()), g.list.h}));
    CHECK(white(g_ifb, {0, 15, g.list.x, int16_t(128 - 15)}));
    // Nothing below the list except the hint line.
    CHECK(white(g_ifb, {0, g.list.bottom(), 296, int16_t(128 - fonts::sans_10.line_height - g.list.bottom())}));
  }
}

// ------------------------------------------- whole pipeline from raw buttons

namespace {
// Raw GPIO samples -> ButtonTracker -> App -> RenderScheduler ->
// DisplayService -> simulated panel, stepped in 5 ms ticks like the device
// (button ISR every 5 ms, main loop and display context polled each tick).
struct System {
  std::atomic<uint32_t> clock{0};
  SimPanel panel{&clock};
  Framebuffer bufs[2];
  JobQueue jobs;
  EventQueue events;
  DisplayService display{panel, bufs, 2, jobs, events};
  RenderScheduler sched{bufs, 2, jobs, events};
  ButtonTracker tracker;
  App app;
  RenderContext ctx;
  std::vector<View> rendered;  // every frame the renderer produced
  std::vector<uint32_t> hashes;  // and its framebuffer hash
  bool sleeping = false;          // main.cpp: kActSleep -> render the sleep view
  uint32_t mask = 0;
  uint8_t speed = 0;  // slowest waveform: 4.5 s per refresh

  System() {
    settings_defaults(&g_is);
    ctx.settings = &g_is;
    display.start(speed);
    tracker.reset(0);
    AppConfig c = icfg(configured_project_count(g_is.profile), 0x5F);
    app.boot(c, -1, 0);
    sched.invalidate();
  }
  static void draw(Framebuffer &fb, void *p) {
    System *s = static_cast<System *>(p);
    const View v = s->sleeping ? s->app.sleep_view() : s->app.view();
    s->rendered.push_back(v);
    render(fb, v, s->ctx);
    s->hashes.push_back(fb.hash());
  }
  // Same handling as apply() in firmware/platform/badger2040/main.cpp.
  void apply(uint32_t a) {
    if (a & kActRedraw) sched.invalidate(a & kActCleanRefresh);
    if (a & kActSleep) {
      sleeping = true;
      sched.invalidate();
    }
  }
  void clear() {
    rendered.clear();
    hashes.clear();
  }
  void run(uint32_t ms) {
    for (uint32_t i = 0; i < ms; i += kSampleMs) {
      clock += kSampleMs;
      const uint32_t t = clock;
      ButtonEvent ev[kMaxEventsPerSample];
      const int n = tracker.sample(mask, t, ev, kMaxEventsPerSample);
      for (int k = 0; k < n; ++k) apply(app.on_button(ev[k]));
      apply(app.on_poll(t));
      sched.poll(draw, this, speed, 3, t);
      display.poll(t);
      sched.poll(draw, this, speed, 3, t);
    }
  }
  void press(Button b, uint32_t hold_ms, uint32_t gap_ms) {
    mask |= 1u << int(b);
    run(hold_ms);
    mask &= ~(1u << int(b));
    run(gap_ms);
  }
  void settle() {
    for (int i = 0; i < 4000 && !sched.settled(); ++i) run(5);
  }
  bool busy() const { return sched.display_active(); }
};
}  // namespace

TEST(pipeline_rapid_index_navigation_during_refresh_shows_only_the_final_project) {
  System s;
  s.settle();
  s.press(Button::C, 100, 0);  // single C (deferred), then
  s.run(kDoublePressMs + 50);
  s.settle();
  CHECK(s.app.view().screen == Screen::Projects);
  // Double C: the index starts refreshing (4.5 s) ...
  s.rendered.clear();
  s.press(Button::C, 80, 100);
  s.press(Button::C, 80, 20);
  CHECK(s.app.view().screen == Screen::Index);
  CHECK(s.busy());
  // ... and, during that refresh, rapid taps and a held DOWN ...
  for (int i = 0; i < 3; ++i) s.press(Button::Down, 60, 60);
  s.press(Button::Down, kRepeatDelayMs + kRepeatIntervalMs + 20, 40);  // 2 repeats
  s.press(Button::Up, 60, 60);
  CHECK_EQ(s.app.index_selection(), 4);
  CHECK(s.busy());  // still the first index refresh
  // ... then confirm right away (acts on the debounced release).
  s.press(Button::C, 80, kDebounceMs);
  CHECK(s.app.view().screen == Screen::Projects);
  s.settle();
  // Two frames: the index as opened, then only the final project page. No
  // intermediate highlight, no intermediate project, nothing stale.
  CHECK_EQ(s.rendered.size(), 2u);
  CHECK(s.rendered[0].screen == Screen::Index && s.rendered[0].index_sel == 0);
  CHECK(s.rendered.back().screen == Screen::Projects && s.rendered.back().project == 4);
  for (const View &v : s.rendered) CHECK(!(v.screen == Screen::Projects && v.project != 4));
  CHECK_EQ(s.panel.violations, 0);
  CHECK(s.panel.image.equals(s.display.shown()));
}

TEST(pipeline_index_redraws_newest_selection_once_after_a_pause) {
  System s;
  s.settle();
  s.press(Button::C, 80, 100);
  s.press(Button::C, 80, 20);  // double C -> index (refreshing)
  s.rendered.clear();
  for (int i = 0; i < 5; ++i) s.press(Button::Down, 50, 50);
  s.run(kIndexSettleMs + 20);  // pause: the newest highlight is queued once
  CHECK(s.busy());
  s.settle();
  CHECK_EQ(s.rendered.size(), 1u);
  CHECK(s.rendered[0].screen == Screen::Index && s.rendered[0].index_sel == 5);
  // Holding DOWN on an idle panel: no frame while held, one after release.
  s.rendered.clear();
  s.mask = 1u << int(Button::Down);
  s.run(2500);
  CHECK(s.rendered.empty());
  CHECK(!s.busy());
  s.mask = 0;
  s.run(kDebounceMs + kIndexSettleMs + 20);
  s.settle();
  CHECK_EQ(s.rendered.size(), 1u);
  CHECK_EQ(s.rendered[0].index_sel, configured_project_count(g_is.profile) - 1);
  CHECK(!s.app.sleeping());  // the long DOWN hold did not power off
  // Cancel while a highlight is queued during a refresh: the queued highlight
  // is discarded and the previous screen comes back.
  s.press(Button::Up, 60, 60);
  s.run(kIndexSettleMs + 20);  // index refresh with the new highlight running
  CHECK(s.busy());
  s.rendered.clear();
  s.press(Button::Up, 60, 60);
  s.press(Button::Up, 60, 60);
  s.press(Button::A, 80, 20);
  s.settle();
  CHECK_EQ(s.rendered.size(), 1u);
  CHECK(s.rendered[0].screen == Screen::Badge);
  CHECK_EQ(s.app.view().project, 0);  // never changed by browsing or cancelling
  CHECK_EQ(s.panel.violations, 0);
}

TEST(pipeline_existing_short_c_and_project_qr_unchanged) {
  System s;
  s.settle();
  s.press(Button::C, 80, kDoublePressMs + 20);
  CHECK(s.app.view().screen == Screen::Projects);
  s.press(Button::Down, 80, 50);
  s.press(Button::B, kLongPressMs + 50, 50);
  CHECK(s.app.view().screen == Screen::ProjectQr);
  CHECK_EQ(s.app.view().project, 1);
  s.press(Button::B, kLongPressMs + 50, 50);
  CHECK(s.app.view().screen == Screen::Projects);
  s.press(Button::B, 80, 50);
  CHECK(s.app.view().screen == Screen::Card);
  s.press(Button::C, 80, kDoublePressMs + 20);
  CHECK(s.app.view().screen == Screen::Projects);
  CHECK_EQ(s.app.view().project, 1);
  // Long C from the project QR: project 1, and no late single on release.
  s.press(Button::B, kLongPressMs + 50, 50);
  CHECK(s.app.view().screen == Screen::ProjectQr);
  s.press(Button::C, kLongPressMs + 300, kDoublePressMs + 100);
  CHECK(s.app.view().screen == Screen::Projects);
  CHECK_EQ(s.app.view().project, 0);
  s.settle();
  CHECK_EQ(s.panel.violations, 0);
}

// ------------------------------------------------- long DOWN in the index

TEST(index_down_hold_powers_off_without_confirming) {
  for (bool to_badge : {true, false}) {
    Drive d;
    AppConfig c = icfg(7);
    c.sleep_to_badge = to_badge;
    d.boot(c);
    d.a.on_screen_request(Screen::Projects, 2);
    d.double_c();
    d.tap(Button::Down);
    d.wait(kIndexSettleMs + 10);  // highlight 4 drawn
    d.redraws.clear();
    d.take();
    // Hold DOWN: scrolls (undrawn), then powers off at kHoldPowerOffMs.
    d.ev(Button::Down, Gesture::Press);
    uint32_t held = 0, next = kRepeatDelayMs;
    uint8_t reps = 0;
    while (held < kHoldPowerOffMs) {
      d.wait(5);
      held += 5;
      if (held == next) {
        d.ev(Button::Down, Gesture::Repeat, ++reps);
        next += kRepeatIntervalMs;
      }
      if (held == kLongPressMs) d.ev(Button::Down, Gesture::Long);
      if (held == kHoldPowerOffMs - 5) CHECK(!d.a.sleeping());  // not one sample early
    }
    CHECK(d.a.index_redraw_pending() && d.a.index_selection() > 3);  // scrolled, not yet drawn
    d.ev(Button::Down, Gesture::Hold);
    CHECK(d.a.sleeping());
    CHECK(d.take() & kActSleep);
    CHECK(!d.a.index_redraw_pending());  // the scrolled highlight is discarded
    CHECK_EQ(d.a.view().project, 2);     // not confirmed: remembered project kept
    CHECK(d.a.view().screen == Screen::Index);
    CHECK(d.redraws.empty());            // no index or project frame before the sleep view
    // The sleep view: the badge, or the index as last drawn (not the scrolled one).
    const View sv = d.a.sleep_view();
    if (to_badge) CHECK(sv.screen == Screen::Badge);
    else CHECK(sv.screen == Screen::Index && sv.index_sel == 3);
    // Nothing fires afterwards: no quiet redraw, no C, no buttons.
    d.wait(3000);
    d.tap(Button::C);
    d.wait(1000);
    CHECK(d.redraws.empty());
  }
}

TEST(index_holds_begun_before_the_index_are_ignored) {
  Drive d;
  d.boot(icfg(7));
  d.a.on_screen_request(Screen::Projects, 2);
  d.ev(Button::Down, Gesture::Press);  // DOWN goes down on the project page ...
  d.wait(50);
  d.double_c();  // ... and the index opens while it is still held
  CHECK(d.a.view().screen == Screen::Index);
  d.wait(1000 - 50 - 400);
  d.ev(Button::Down, Gesture::Long);   // would be power-off outside the index
  d.ev(Button::Down, Gesture::Repeat, 1);
  d.ev(Button::Down, Gesture::Hold);
  d.ev(Button::Down, Gesture::Short, 1);  // (a stray release)
  CHECK(!d.a.sleeping());
  CHECK_EQ(d.a.index_selection(), 2);  // did not scroll
  // A fresh press in the index works normally.
  d.tap(Button::Down);
  CHECK_EQ(d.a.index_selection(), 3);
}

TEST(pipeline_index_down_hold_runs_the_power_off_sequence) {
  System s;
  s.settle();
  s.press(Button::C, 80, 100);
  s.press(Button::C, 80, 20);  // double C -> index
  s.settle();
  s.clear();
  s.mask = 1u << int(Button::Down);
  s.run(kDebounceMs + kHoldPowerOffMs - 10);
  CHECK(!s.app.sleeping());
  CHECK(s.rendered.empty());  // the scrolled highlight is never drawn
  s.run(20);
  CHECK(s.app.sleeping());
  s.run(500);
  s.mask = 0;
  s.run(1000);
  s.settle();
  // Exactly one more frame: the sleep view (badge), fully on the panel.
  CHECK_EQ(s.rendered.size(), 1u);
  CHECK(s.rendered[0].screen == Screen::Badge);
  CHECK(s.sched.settled());
  CHECK_EQ(s.app.view().project, 0);
  CHECK_EQ(s.panel.violations, 0);
}

// ------------------------------------------------------------ USR

TEST(pipeline_usr_short_diagnostics_long_layout_without_diagnostics) {
  System s;
  s.settle();
  const uint8_t layout0 = s.app.view().layout;
  // Long USR on the badge: layout toggles; diagnostics never opens, not even
  // briefly (the tracker sends no Short after a Long).
  s.clear();
  s.press(Button::User, kLongPressMs + 300, 200);
  s.settle();
  CHECK(s.app.view().screen == Screen::Badge);
  CHECK(s.app.view().layout != layout0);
  CHECK_EQ(s.rendered.size(), 1u);
  CHECK(s.rendered[0].screen == Screen::Badge && s.rendered[0].layout != layout0);
  // Just below the long threshold: diagnostics, layout unchanged.
  s.clear();
  s.press(Button::User, kLongPressMs - 10, 100);
  s.settle();
  CHECK(s.app.view().screen == Screen::Info);
  CHECK(s.app.view().layout != layout0);
  CHECK_EQ(s.rendered.size(), 1u);
  // Long USR on diagnostics: still no extra frame of anything, layout back.
  s.clear();
  s.press(Button::User, kLongPressMs + 50, 100);
  s.settle();
  CHECK(s.app.view().screen == Screen::Info);
  CHECK_EQ(s.app.view().layout, layout0);
  for (const View &v : s.rendered) CHECK(v.screen == Screen::Info);
  // Short USR from the card: diagnostics.
  s.press(Button::B, 80, 100);
  s.press(Button::User, 80, 100);
  CHECK(s.app.view().screen == Screen::Info);
}

// ------------------------------------------------ pending single C policy

namespace {
// Single C tapped on the card with project `remembered`, then `fn` within
// the double-press window. Returns the redraws that followed.
template <typename F>
std::vector<View> after_c(Drive &d, int remembered, F fn, uint32_t gap = 100) {
  d.a.on_screen_request(Screen::Projects, remembered);
  d.a.on_screen_request(Screen::Card);
  d.redraws.clear();
  d.take();
  d.tap(Button::C);
  CHECK(d.a.click_pending());
  d.wait(gap);
  fn();
  d.wait(2000);
  CHECK(!d.a.click_pending());
  CHECK(!d.a.click_frozen());
  return d.redraws;
}
bool has_projects(const std::vector<View> &r) {
  for (const View &v : r)
    if (v.screen == Screen::Projects) return true;
  return false;
}
}  // namespace

TEST(pending_c_dropped_by_screen_choices) {
  Drive d;
  d.boot(icfg(7));
  // A short: the badge, and the project page is never requested.
  auto r = after_c(d, 3, [&] { d.tap(Button::A); });
  CHECK(d.a.view().screen == Screen::Badge);
  CHECK_EQ(r.size(), 1u);
  CHECK(!has_projects(r));
  // B short: the card (already there: nothing at all).
  r = after_c(d, 3, [&] { d.tap(Button::B); });
  CHECK(d.a.view().screen == Screen::Card);
  CHECK(r.empty());
  // USR short: diagnostics.
  r = after_c(d, 3, [&] { d.tap(Button::User); });
  CHECK(d.a.view().screen == Screen::Info);
  CHECK_EQ(r.size(), 1u);
  CHECK(!has_projects(r));
  // DOWN long: power off, no project first.
  r = after_c(d, 3, [&] { d.hold(Button::Down); });
  CHECK(d.a.sleeping());
  CHECK(!has_projects(r));
  // A swipe: its own screen choice, from where the visitor was.
  d.boot(icfg(7));
  d.a.set_gesture_mode(true);
  r = after_c(d, 3, [&] { d.note(d.a.on_swipe(Swipe::Right, d.t)); });
  CHECK(d.a.view().screen == Screen::Projects);  // card -> next screen
  CHECK_EQ(r.size(), 1u);
  r = after_c(d, 3, [&] { d.note(d.a.on_swipe(Swipe::Down, d.t)); });
  CHECK(d.a.view().screen == Screen::Badge);
  CHECK(!has_projects(r));
  CHECK_EQ(d.a.view().project, 3);  // remembered project untouched throughout
}

TEST(pending_c_applies_first_for_actions_on_its_page) {
  Drive d;
  d.boot(icfg(7, 0x5F));  // project 6 (index 5) has no link
  // Long B: the remembered project's QR, in one step (no project frame first),
  // even though B was pressed inside the window and held past it.
  auto r = after_c(d, 3, [&] { d.hold(Button::B); });
  CHECK(d.a.view().screen == Screen::ProjectQr);
  CHECK_EQ(d.a.view().project, 3);
  CHECK_EQ(r.size(), 1u);
  CHECK(r[0].screen == Screen::ProjectQr && r[0].project == 3);
  // ... and for a project without a link: its page, never the contact QR.
  r = after_c(d, 5, [&] { d.hold(Button::B); });
  CHECK(d.a.view().screen == Screen::Projects);
  CHECK_EQ(d.a.view().project, 5);
  // DOWN / UP short: step from the remembered project, one frame.
  r = after_c(d, 3, [&] { d.tap(Button::Down); });
  CHECK(d.a.view().screen == Screen::Projects);
  CHECK_EQ(d.a.view().project, 4);
  CHECK_EQ(r.size(), 1u);
  CHECK_EQ(r[0].project, 4);
  r = after_c(d, 3, [&] { d.tap(Button::Up); });
  CHECK_EQ(d.a.view().project, 2);
  // A long: the project page with a clean refresh.
  d.a.on_screen_request(Screen::Projects, 3);
  d.a.on_screen_request(Screen::Card);
  d.tap(Button::C);
  d.wait(100);
  d.take();
  d.hold(Button::A);
  CHECK((d.take() & (kActRedraw | kActCleanRefresh)) == (kActRedraw | kActCleanRefresh));
  CHECK(d.a.view().screen == Screen::Projects);
  // UP long / USR long: the project opens and the toggle still happens.
  r = after_c(d, 3, [&] { d.hold(Button::Up); });
  CHECK(d.a.view().screen == Screen::Projects && d.a.gesture_mode());
  const uint8_t l = d.a.view().layout;
  r = after_c(d, 3, [&] { d.hold(Button::User); });
  CHECK(d.a.view().screen == Screen::Projects && d.a.view().layout != l);
}

TEST(pending_c_window_freezes_while_another_button_decides) {
  Drive d;
  d.boot(icfg(7));
  d.a.on_screen_request(Screen::Projects, 3);
  d.a.on_screen_request(Screen::Card);
  d.redraws.clear();
  d.tap(Button::C);
  d.wait(kDoublePressMs - 50);
  d.ev(Button::B, Gesture::Press);  // inside the window
  d.wait(500);                      // the window would have expired here
  CHECK(d.redraws.empty());
  CHECK(d.a.click_frozen());
  d.wait(kLongPressMs - 500);
  d.ev(Button::B, Gesture::Long);
  CHECK(d.a.view().screen == Screen::ProjectQr);
  CHECK_EQ(d.redraws.size(), 1u);
  // A press after the window expired: the C has already acted (one frame),
  // then the button acts on that page as usual.
  d.a.on_screen_request(Screen::Card);
  d.redraws.clear();
  d.tap(Button::C);
  d.wait(kDoublePressMs + 10);
  CHECK(d.a.view().screen == Screen::Projects);
  d.tap(Button::A);
  CHECK(d.a.view().screen == Screen::Badge);
  CHECK_EQ(d.redraws.size(), 2u);
  // The deciding gesture never arrives (lost events): the C is dropped, not
  // fired late.
  d.a.on_screen_request(Screen::Card);
  d.redraws.clear();
  d.tap(Button::C);
  d.wait(100);
  d.ev(Button::B, Gesture::Press);
  d.wait(kLongPressMs + 400);
  CHECK(!d.a.click_frozen());
  CHECK(!d.a.click_pending());
  d.wait(2000);
  CHECK(d.redraws.empty());
  CHECK(d.a.view().screen == Screen::Card);
}

TEST(pipeline_rapid_c_then_a_never_submits_a_project_frame) {
  System s;
  s.settle();
  s.press(Button::B, 80, 100);  // card
  s.settle();
  s.clear();
  const uint32_t submitted = s.sched.submitted();
  s.press(Button::C, 80, 120);  // C, then A well inside the double-press window
  s.press(Button::A, 80, 100);
  s.run(2000);
  s.settle();
  CHECK(s.app.view().screen == Screen::Badge);
  CHECK_EQ(s.rendered.size(), 1u);
  CHECK(s.rendered[0].screen == Screen::Badge);
  for (const View &v : s.rendered) CHECK(v.screen != Screen::Projects);
  CHECK_EQ(s.sched.submitted() - submitted, 1u);
  CHECK_EQ(s.panel.violations, 0);
}

TEST(pipeline_c_then_long_b_shows_the_remembered_projects_qr) {
  System s;
  CHECK(settings_set(&g_is, *find_field("qr.payload"), "https://example.com/contact") == SetResult::Ok);
  s.settle();
  // Remember project 2 (Chamber Heater), then go to the card.
  s.press(Button::C, 80, kDoublePressMs + 20);
  s.press(Button::Down, 80, 100);
  s.press(Button::B, 80, 100);
  s.settle();
  CHECK_EQ(s.app.view().project, 1);
  s.clear();
  s.press(Button::C, 80, 150);              // tap C, then within the window ...
  s.press(Button::B, kLongPressMs + 50, 100);  // ... hold B
  s.settle();
  CHECK(s.app.view().screen == Screen::ProjectQr);
  CHECK_EQ(s.rendered.size(), 1u);  // straight to the QR, no project page first
  // Its payload is that project's own link, not the contact QR.
  const char *url = project_url(g_is.profile, 1);
  CHECK(url != nullptr);
  CHECK_STR(url, "https://example.com/alex/chamber-heater");
  CHECK(project_qr_geometry(s.ctx, 1).qr_status == QrStatus::Ok);
  Framebuffer want, contact;
  View v;
  v.screen = Screen::ProjectQr;
  v.project = 1;
  v.layout = s.app.view().layout;
  render(want, v, s.ctx);
  v.screen = Screen::QrFull;
  render(contact, v, s.ctx);
  CHECK_EQ(s.hashes.back(), want.hash());
  CHECK(s.hashes.back() != contact.hash());
  CHECK(s.panel.image.equals(want));
  settings_defaults(&g_is);
}

TEST(pending_c_frozen_then_c_again) {
  Drive d;
  d.boot(icfg(7));
  d.a.on_screen_request(Screen::Projects, 3);
  d.a.on_screen_request(Screen::Card);
  // C, B goes down (freezes), C again long after the window: a new single C
  // interaction, which the stale freeze deadline must not cancel.
  d.tap(Button::C);
  d.wait(100);
  d.ev(Button::B, Gesture::Press);
  d.wait(600);
  d.redraws.clear();
  d.tap_c();
  CHECK(d.a.view().screen == Screen::Projects);
  CHECK_EQ(d.a.view().project, 3);
  CHECK_EQ(d.redraws.size(), 1u);
  d.wait(kLongPressMs + 500);  // past the old freeze deadline: nothing more
  CHECK_EQ(d.redraws.size(), 1u);
  // C, B down, C inside the window: still a double press (the index).
  d.a.on_screen_request(Screen::Card);
  d.tap(Button::C);
  d.wait(50);
  d.ev(Button::B, Gesture::Press);
  d.wait(50);
  d.tap(Button::C);
  CHECK(d.a.view().screen == Screen::Index);
  d.wait(kLongPressMs + 500);
  CHECK(d.a.view().screen == Screen::Index);
}
