#include <atomic>
#include <initializer_list>
#include <thread>

#include "check.hpp"
#include "display_pipeline.hpp"
#include "sim_panel.hpp"

using namespace badge;

namespace {
struct Scene {
  int value = 0;       // what to draw
  int renders = 0;
};
void draw_scene(Framebuffer &fb, void *ctx) {
  Scene *s = static_cast<Scene *>(ctx);
  ++s->renders;
  fb.clear(Ink::White);
  // A small block whose position encodes the value; a big bar for value >= 100.
  if (s->value >= 100) fb.fill_rect({0, 0, 296, 128}, Ink::Black);
  fb.fill_rect({int16_t(10 + (s->value % 50) * 4), 40, 8, 8}, Ink::Black);
}

struct Rig {
  std::atomic<uint32_t> clock{0};
  SimPanel panel{&clock};
  Framebuffer bufs[2];
  JobQueue jobs;
  EventQueue events;
  DisplayService display{panel, bufs, 2, jobs, events};
  RenderScheduler sched{bufs, 2, jobs, events};
  Scene scene;
  uint8_t speed = 1, max_partials = 3;
  Rig() { display.start(speed); }
  void step(uint32_t ms = 10) {
    for (uint32_t i = 0; i < ms; i += 10) {
      clock += 10;
      sched.poll(draw_scene, &scene, speed, max_partials);
      display.poll(clock);
      sched.poll(draw_scene, &scene, speed, max_partials);
    }
  }
  void settle() { for (int i = 0; i < 3000 && !sched.settled(); ++i) step(); }
  int count(char k) const { int n = 0; for (auto &o : panel.ops) n += o.kind == k; return n; }
};
}  // namespace

TEST(pipeline_first_frame_is_full_refresh) {
  Rig r;
  r.sched.invalidate();
  r.settle();
  CHECK(r.sched.settled());
  CHECK_EQ(r.count('F'), 1);
  CHECK_EQ(r.count('O'), 1);  // booster powered off after refresh
  CHECK_EQ(r.panel.violations, 0);
  CHECK(r.panel.image.equals(r.display.shown()));
  CHECK_EQ(r.sched.free_count(), 2);
}

TEST(pipeline_unchanged_screen_is_suppressed) {
  Rig r;
  r.sched.invalidate();
  r.settle();
  const size_t ops = r.panel.ops.size();
  for (int i = 0; i < 5; ++i) { r.sched.invalidate(); r.settle(); }
  CHECK_EQ(r.panel.ops.size(), ops);  // no panel traffic at all
  CHECK_EQ(r.sched.suppressed(), 5u);
}

TEST(pipeline_coalesces_requests_while_busy) {
  Rig r;
  r.speed = 0;  // 4.5 s refreshes
  r.display.start(0);
  r.sched.invalidate();
  r.step(50);  // first refresh running
  for (int v = 1; v <= 40; ++v) {  // a burst of button presses during the refresh
    r.scene.value = 100 + v;
    r.sched.invalidate();
    r.step(20);
  }
  r.settle();
  CHECK_EQ(r.panel.violations, 0);
  // Only the first frame and the final frame reach the panel.
  CHECK_EQ(r.count('F') + r.count('P'), 2);
  CHECK(r.scene.renders <= 2);
  CHECK(r.sched.coalesced() > 30u);
  static Framebuffer want;
  draw_scene(want, &r.scene);
  CHECK(r.panel.image.equals(want));  // newest view wins
  CHECK_EQ(r.sched.free_count(), 2);
}

TEST(pipeline_partial_for_small_changes_then_forced_full) {
  Rig r;
  r.sched.invalidate();
  r.settle();
  for (int v = 1; v <= 5; ++v) {
    r.scene.value = v;
    r.sched.invalidate();
    r.settle();
  }
  // max_partials = 3: P P P F P
  int partial = 0, full = 0;
  for (auto &o : r.panel.ops) {
    if (o.kind == 'P') { ++partial; CHECK(o.r.y % 8 == 0 && o.r.h % 8 == 0); }
    if (o.kind == 'F') ++full;
  }
  CHECK_EQ(partial, 4);
  CHECK_EQ(full, 2);
  CHECK_EQ(r.panel.violations, 0);
  // Large change -> full refresh.
  r.scene.value = 100;
  r.sched.invalidate();
  r.settle();
  CHECK(r.panel.ops[r.panel.ops.size() - 2].kind == 'F');
}

TEST(pipeline_clean_refresh_uses_otp_waveform_then_restores_speed) {
  Rig r;
  r.sched.invalidate();
  r.settle();
  r.sched.invalidate(true);  // same image, but clean requested
  r.settle();
  CHECK_EQ(r.count('F'), 2);
  CHECK(r.panel.speed() == 0);
  r.scene.value = 100;
  r.sched.invalidate();
  r.settle();
  CHECK(r.panel.speed() == 1);  // back to the configured speed
  CHECK_EQ(r.display.stats().clean, 1u);
}

TEST(pipeline_busy_timeout_recovers) {
  Rig r;
  r.sched.invalidate();
  r.step(20);
  r.panel.stuck = true;  // BUSY line never releases
  r.step(DisplayService::kBusyTimeoutMs + 200);
  CHECK_EQ(r.display.stats().timeouts, 1u);
  CHECK_EQ(r.sched.timeouts(), 1u);
  CHECK_EQ(r.count('I'), 2);  // re-initialised
  r.settle();                  // the scheduler redraws after a timeout
  CHECK(r.sched.settled());
  CHECK_EQ(r.sched.free_count(), 2);
  CHECK(r.panel.ops.back().kind == 'O');
}

TEST(pipeline_dual_thread_ownership) {
  // Display service on its own thread (as on core 1); scheduler on this one.
  std::atomic<uint32_t> clock{0};
  SimPanel panel{&clock};
  static Framebuffer bufs[2];
  static JobQueue jobs;
  static EventQueue events;
  DisplayService display{panel, bufs, 2, jobs, events};
  RenderScheduler sched{bufs, 2, jobs, events};
  Scene scene;
  display.start(3);
  std::atomic<bool> stop{false};
  std::thread core1([&] {
    while (!stop) { display.poll(clock); std::this_thread::yield(); }
  });
  for (int i = 0; i < 3000; ++i) {
    clock += 1;
    if (i % 7 == 0) { scene.value = (scene.value + 13) % 150; sched.invalidate(i % 50 == 0); }
    sched.poll(draw_scene, &scene, 3, 2);
    std::this_thread::yield();
  }
  for (int i = 0; i < 200000 && !sched.settled(); ++i) {
    clock += 1;
    sched.poll(draw_scene, &scene, 3, 2);
    std::this_thread::yield();
  }
  stop = true;
  core1.join();
  CHECK(sched.settled());
  CHECK_EQ(panel.violations, 0);
  CHECK_EQ(display.stats().event_overflows, 0u);
  CHECK_EQ(sched.free_count(), 2);
  static Framebuffer want;
  draw_scene(want, &scene);
  CHECK(panel.image.equals(want));
}

// BUSY held low for good (controller dead or shorted): the busy timeout's
// re-init fails in bounded time, the display context keeps polling (watchdog
// heartbeat), the app side never waits on a refresh that cannot happen, and
// re-init is retried with backoff instead of hammering the panel.
TEST(pipeline_dead_panel_never_blocks_and_recovers_with_one_clean_redraw) {
  // Speed 0 matters: its clean redraw needs no speed change, so only the
  // busy-timeout path itself can notice that re-init failed.
  for (uint8_t speed : {uint8_t(0), uint8_t(1)}) {
  Rig r;
  r.speed = speed;
  r.display.start(speed);
  r.sched.invalidate();
  r.step(20);
  r.panel.dead = true;
  const uint32_t hb0 = r.display.heartbeat();
  r.step(DisplayService::kBusyTimeoutMs + 500);
  CHECK(!r.display.panel_ok());
  CHECK_EQ(r.display.stats().timeouts, 1u);
  CHECK(r.display.heartbeat() - hb0 >= (DisplayService::kBusyTimeoutMs + 500) / 10);
  r.settle();
  CHECK(r.sched.settled());  // redraw request consumed without the panel
  CHECK_EQ(r.sched.free_count(), 2);
  // Navigation while the panel is dead: views are rendered and released at
  // once; nothing is sent to the panel.
  const int full_before = r.count('F') + r.count('P');
  for (int v = 1; v <= 10; ++v) {
    r.scene.value = v;
    r.sched.invalidate();
    r.settle();
    CHECK(r.sched.settled());
  }
  CHECK_EQ(r.count('F') + r.count('P'), full_before);
  // Five minutes dead: re-init attempts back off (2, 4, 8 ... 60 s).
  const int inits_before = r.count('I');
  r.step(300000);
  const int retries = r.count('I') - inits_before;
  CHECK(retries >= 4 && retries <= 12);
  CHECK_EQ(r.display.stats().timeouts, 1u);  // no refresh is ever started on the dead panel
  CHECK_EQ(r.display.stats().event_overflows, 0u);
  // The controller answers again: exactly one clean full refresh of the
  // newest view, although that frame was already "submitted" while dead.
  r.panel.dead = false;
  r.step(DisplayService::kPanelRetryMaxMs + 100);
  r.settle();
  CHECK(r.display.panel_ok());
  CHECK_EQ(r.count('F') + r.count('P'), full_before + 1);
  CHECK_EQ(r.display.stats().clean, 1u);
  static Framebuffer want;
  draw_scene(want, &r.scene);
  CHECK(r.panel.image.equals(want));
  CHECK_EQ(r.panel.violations, 0);
  }
}

TEST(pipeline_panel_unresponsive_at_boot_does_not_wait_for_timeouts) {
  Rig r;
  r.panel.dead = true;
  r.display.start(1);  // bounded reset fails
  CHECK(!r.display.panel_ok());
  r.sched.invalidate();
  r.step(100);  // far below the 15 s busy timeout
  CHECK(r.sched.settled());
  CHECK_EQ(r.count('F') + r.count('P'), 0);
  r.panel.dead = false;
  r.step(DisplayService::kPanelRetryMinMs + 100);
  r.settle();
  CHECK(r.display.panel_ok());
  CHECK_EQ(r.count('F'), 1);
  static Framebuffer want;
  draw_scene(want, &r.scene);
  CHECK(r.panel.image.equals(want));
}

// A speed change resets the controller; if that reset fails the job is
// completed without a refresh instead of driving a controller in reset.
TEST(pipeline_failed_speed_change_is_a_panel_fault_not_a_hang) {
  Rig r;
  r.sched.invalidate();
  r.settle();
  r.panel.dead = true;
  r.sched.invalidate(true);  // clean refresh needs speed 0 -> set_speed
  r.step(50);
  CHECK(!r.display.panel_ok());
  CHECK(r.sched.settled());
  CHECK_EQ(r.count('F'), 1);
  CHECK_EQ(r.panel.violations, 0);
}
