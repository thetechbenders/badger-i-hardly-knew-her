// Badger 2040 photo badge / business card firmware - RP2040 entry point.
//
// Execution contexts and ownership
//   core 0  main loop: app state machine, renderer (RenderScheduler), USB CDC
//           CLI, settings service + flash commits, battery/VBUS sampling,
//           watchdog, LED. Owns the Settings objects.
//   IRQ     5 ms repeating timer on core 0: samples buttons, runs the
//           debouncer, pushes ButtonEvents into an SPSC queue (ISR -> core 0).
//   core 1  display service: the only context that touches the UC8151 panel
//           (spi0, CS/DC/RESET/BUSY). In single-core diagnostic mode core 1 is
//           not started and core 0 polls the same DisplayService instead.
#include <cstdio>
#include <cstring>

#include "app.hpp"
#include "assetpack.hpp"
#include "board.hpp"
#include "build_info.hpp"
#include "cli.hpp"
#include "diagnostics.hpp"
#include "display_pipeline.hpp"
#include "flash_layout.hpp"
#include "flash_rp2040.hpp"
#include "hardware/sync.h"
#include "panel_uc8151.hpp"
#include "pico/flash.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "renderer.hpp"
#include "settings_store.hpp"
#include "text.hpp"

namespace badge {
extern const uint8_t kBuiltinAssetPack[];
extern const size_t kBuiltinAssetPack_size;
}  // namespace badge

using namespace badge;

namespace {

// ------------------------------------------------------------------ state
Settings g_committed;  // last persisted (or defaults)
Settings g_staged;     // what the CLI edits and the renderer shows
bool g_staged_dirty = false;
Rp2040Flash *g_flash;
SettingsStore *g_store;

Framebuffer g_bufs[2];
JobQueue g_jobs;
EventQueue g_events;
Uc8151Panel g_panel;
DisplayService g_display(g_panel, g_bufs, 2, g_jobs, g_events);
RenderScheduler g_sched(g_bufs, 2, g_jobs, g_events);
bool g_single_core = false;
volatile bool g_core1_ready = false;
uint8_t g_core1_speed = 1;  // written before core 1 launches, read-only after

SpscQueue<ButtonEvent, 16> g_buttons;  // timer ISR -> main loop
ButtonTracker g_tracker;               // owned by the timer ISR after start
repeating_timer_t g_button_timer;

App g_app;
RenderContext g_ctx;
InfoLines g_info_lines;
const uint8_t *g_asset_base = nullptr;
AssetPackInfo g_asset_info;
const char *g_asset_source = "none";

uint32_t g_vsys_mv = 0;
bool g_usb = true;

enum class SleepPhase : uint8_t { None, ShowSleepView, PowerOff };
SleepPhase g_sleep = SleepPhase::None;

uint32_t now_ms() { return to_ms_since_boot(get_absolute_time()); }

// ------------------------------------------------------------- inputs
bool button_timer_cb(repeating_timer_t *) {
  ButtonEvent ev[4];
  const int n = g_tracker.sample(board::read_buttons(), now_ms(), ev, 4);
  for (int i = 0; i < n; ++i) g_buttons.push(ev[i]);  // full queue: drop (user is mashing)
  return true;
}

// ------------------------------------------------------------ display
void core1_main() {
  // Register as a lockout victim so core 0 can safely write flash.
  flash_safe_execute_core_init();
  diag::paint_stacks();
  g_display.start(g_core1_speed);
  g_core1_ready = true;
  while (true) {
    const bool busy = g_display.poll(now_ms());
    __sev();  // wake core 0 for any events just queued
    sleep_ms(busy ? 10 : 5);
  }
}

AppConfig app_config(bool safe) {
  AppConfig c;
  const Settings &s = g_staged;
  c.project_count = uint8_t(configured_project_count(s.profile));
  c.qr_configured = qr_configured(s.profile);
  c.layout = s.prefs.layout;
  c.safe_mode = safe;
  c.sleep_timeout_s = s.prefs.sleep_timeout_s;
  c.sleep_to_badge = s.prefs.sleep_screen == uint8_t(SleepScreen::Badge);
  c.wake_selects_screen = s.prefs.wake_selects_screen;
  return c;
}

void build_info_lines() {
  InfoLines &L = g_info_lines;
  L.count = 0;
  const diag::BootInfo &bi = diag::info();
  const diag::MemReport m = diag::memory();
  L.add("Firmware  %s  %s", build::kVersion, build::kBuildType);
  L.add("Deps      pico-sdk %s | pimoroni-pico %s", build::kPicoSdk, build::kPimoroni);
  L.add("Power     %s | VSYS %lu.%02lu V", g_usb ? "USB" : "battery", (unsigned long)(g_vsys_mv / 1000),
        (unsigned long)(g_vsys_mv % 1000 / 10));
  L.add("Reset     %s | boot %lu | streak %lu", diag::reset_kind_str(bi.kind), (unsigned long)bi.boot_count,
        (unsigned long)bi.crash_streak);
  if (bi.message[0]) L.add("Last      %s", bi.message);
  const StoreStatus &st = g_store->status();
  L.add("Settings  %s seq %lu%s%s", st.active_slot < 0 ? "defaults" : (st.active_slot ? "slot B" : "slot A"),
        (unsigned long)st.sequence, st.recovered ? " | recovered" : "", g_staged_dirty ? " | UNSAVED" : "");
  L.add("Assets    %s: %s", g_asset_source, asset_status_str(g_asset_info.status));
  const DisplayStats &ds = g_display.stats();
  L.add("Display   %s | full %lu part %lu skip %lu", g_single_core ? "single-core" : "dual-core",
        (unsigned long)ds.full + ds.clean, (unsigned long)ds.partial,
        (unsigned long)(ds.suppressed + g_sched.suppressed()));
  L.add("Memory    flash %lu K | stack free %lu/%lu B", (unsigned long)(m.flash_used / 1024),
        (unsigned long)m.stack0_min_free, (unsigned long)m.stack1_min_free);
}

void render_cb(Framebuffer &fb, void *) {
  const View v = g_sleep == SleepPhase::None ? g_app.view() : g_app.sleep_view();
  if (v.screen == Screen::Info || v.screen == Screen::Recovery) build_info_lines();
  g_ctx.battery_low = !g_usb && g_staged.prefs.battery_low_mv && g_vsys_mv &&
                      g_vsys_mv < g_staged.prefs.battery_low_mv;
  render(fb, v, g_ctx);
}

void apply(uint32_t actions) {
  if (actions & kActRedraw) g_sched.invalidate(actions & kActCleanRefresh);
  if (actions & kActSleep) g_sleep = SleepPhase::ShowSleepView, g_sched.invalidate();
}

// ------------------------------------------------------------- assets
void select_assets() {
  const uint8_t *flash_pack = reinterpret_cast<const uint8_t *>(XIP_BASE + flash_layout::kAssetOffset);
  AssetPackInfo fi = asset_pack_validate(flash_pack, flash_layout::kAssetSize);
  if (fi.status == AssetStatus::Ok) {
    g_asset_base = flash_pack;
    g_asset_info = fi;
    g_asset_source = "flash pack";
  } else {
    AssetPackInfo bi = asset_pack_validate(kBuiltinAssetPack, kBuiltinAssetPack_size);
    g_asset_info = bi;
    // Report a corrupt flash pack even though the built-in one is used.
    g_asset_source = fi.status == AssetStatus::Missing ? "built-in" : "built-in (flash pack invalid)";
    if (bi.status == AssetStatus::Ok) g_asset_base = kBuiltinAssetPack;
  }
  g_ctx.portrait = MonoBitmap{};
  if (g_asset_base) asset_pack_bitmap(g_asset_base, kAssetIdPortrait, &g_ctx.portrait);
}

// ---------------------------------------------------------------- CLI
class UsbCliHost : public CliHost {
 public:
  void write(const char *s) override { std::fputs(s, stdout); }
  Settings &staged() override { return g_staged; }
  const Settings &committed() const override { return g_committed; }
  void staged_changed() override {
    g_staged_dirty = std::memcmp(&g_staged, &g_committed, sizeof g_staged) != 0;
    apply(g_app.on_config_changed(app_config(diag::info().safe_mode)));
  }
  bool commit(char *err, size_t errlen) override {
    const FieldDesc *bad = nullptr;
    if (!settings_validate(g_staged, &bad)) {
      std::snprintf(err, errlen, "invalid field %s", bad ? bad->key : "?");
      return false;
    }
    diag::feed_watchdog();
    if (!g_store->commit(g_staged)) {
      std::snprintf(err, errlen, "flash write/verify failed (rc %d); previous settings kept", g_flash->last_error());
      return false;
    }
    g_committed = g_staged;
    g_staged_dirty = false;
    std::printf("saved to slot %c seq %lu (%lu us)\r\n", g_store->status().active_slot ? 'B' : 'A',
                (unsigned long)g_store->status().sequence, (unsigned long)g_flash->last_duration_us());
    if (diag::info().safe_mode) std::printf("safe mode: reboot to leave safe mode\r\n");
    return true;
  }
  void revert() override {
    g_staged = g_committed;
    staged_changed();
  }
  bool select_screen(Screen s, int project) override {
    if (s == Screen::Projects && configured_project_count(g_staged.profile) == 0) return false;
    if (s == Screen::Projects && project >= configured_project_count(g_staged.profile)) return false;
    if (s == Screen::QrFull && !qr_configured(g_staged.profile)) return false;
    apply(g_app.on_screen_request(s, project));
    return true;
  }
  bool project_step(int d) override {
    if (configured_project_count(g_staged.profile) < 2) return false;
    apply(g_app.on_project_step(d));
    return true;
  }
  void refresh(bool clean) override { g_sched.invalidate(clean); }
  void print_version() override {
    std::printf("%s %s (%s)\r\nbuilt %s\r\npico-sdk %s, pimoroni-pico %s, %s\r\n", build::kName, build::kVersion,
                build::kBuildType, build::kDate, build::kPicoSdk, build::kPimoroni, build::kCompiler);
  }
  void print_status() override {
    const View v = g_app.view();
    std::printf("screen %s project %u layout %c\r\n", screen_name(v.screen), v.project + 1, v.layout ? 'B' : 'A');
    std::printf("power %s vsys %lu mV | idle %lu s | settings %s\r\n", g_usb ? "usb" : "battery",
                (unsigned long)g_vsys_mv, (unsigned long)(g_app.idle_ms(now_ms()) / 1000),
                g_staged_dirty ? "UNSAVED changes" : "saved");
    std::printf("display %s | %s\r\n", g_display.busy() ? "busy" : "idle", g_single_core ? "single-core" : "dual-core");
  }
  bool print_diag(const char *t) override {
    const bool all = !std::strcmp(t, "all");
    bool known = all;
    if (all || !std::strcmp(t, "reset")) {
      known = true;
      const diag::BootInfo &b = diag::info();
      std::printf("reset: %s, boot %lu since power-on, crash streak %lu%s\r\n", diag::reset_kind_str(b.kind),
                  (unsigned long)b.boot_count, (unsigned long)b.crash_streak, b.safe_mode ? ", SAFE MODE" : "");
      if (b.message[0]) std::printf("reset: last message: %s (pc %08lx)\r\n", b.message, (unsigned long)b.fault_pc);
    }
    if (all || !std::strcmp(t, "mem")) {
      known = true;
      const diag::MemReport m = diag::memory();
      std::printf("mem: flash image %lu B (limit %lu), static RAM %lu B, heap free ~%lu B\r\n",
                  (unsigned long)m.flash_used, (unsigned long)flash_layout::kFirmwareLimit,
                  (unsigned long)m.ram_static, (unsigned long)m.heap_free_est);
      std::printf("mem: stack core0 %lu B (min free %lu), core1 %lu B (min free %lu)\r\n",
                  (unsigned long)m.stack0_size, (unsigned long)m.stack0_min_free, (unsigned long)m.stack1_size,
                  (unsigned long)m.stack1_min_free);
    }
    if (all || !std::strcmp(t, "flash") || !std::strcmp(t, "settings")) {
      known = true;
      const StoreStatus &st = g_store->status();
      for (int i = 0; i < 2; ++i)
        std::printf("settings: slot %c %s seq %lu unknown %u rejected %u\r\n", 'A' + i,
                    decode_status_str(st.slot_info[i].status), (unsigned long)st.slot_info[i].sequence,
                    st.slot_info[i].unknown_fields, st.slot_info[i].rejected_fields);
      std::printf("settings: active %d seq %lu commits %lu failures %lu%s%s\r\n", st.active_slot,
                  (unsigned long)st.sequence, (unsigned long)st.commits, (unsigned long)st.commit_failures,
                  st.recovered ? " (recovered from corrupt slot)" : "", g_staged_dirty ? " UNSAVED" : "");
      std::printf("flash: settings @0x%06lx x2 sectors, assets @0x%06lx (%lu KiB)\r\n",
                  (unsigned long)flash_layout::kSettingsOffset, (unsigned long)flash_layout::kAssetOffset,
                  (unsigned long)(flash_layout::kAssetSize / 1024));
    }
    if (all || !std::strcmp(t, "assets")) {
      known = true;
      std::printf("assets: %s: %s, %u entries, %lu B, crc %08lx; portrait %ux%u\r\n", g_asset_source,
                  asset_status_str(g_asset_info.status), g_asset_info.entries, (unsigned long)g_asset_info.total_size,
                  (unsigned long)g_asset_info.data_crc, g_ctx.portrait.width, g_ctx.portrait.height);
    }
    if (all || !std::strcmp(t, "display")) {
      known = true;
      const DisplayStats &d = g_display.stats();
      std::printf("display: %s, %s; full %lu clean %lu partial %lu; panel-skipped %lu, dropped %lu, timeouts %lu\r\n",
                  g_single_core ? "single-core" : "dual-core", g_display.busy() ? "busy" : "idle",
                  (unsigned long)d.full, (unsigned long)d.clean, (unsigned long)d.partial,
                  (unsigned long)d.suppressed, (unsigned long)d.dropped, (unsigned long)d.timeouts);
      std::printf("display: last %lu ms, max %lu ms; renders suppressed %lu, coalesced %lu, event overflow %lu\r\n",
                  (unsigned long)d.last_ms, (unsigned long)d.max_ms, (unsigned long)g_sched.suppressed(),
                  (unsigned long)g_sched.coalesced(), (unsigned long)d.event_overflows);
      std::printf("queues: jobs %lu/%lu, events %lu/%lu, buttons %lu/%lu (dropped %lu)\r\n",
                  (unsigned long)g_jobs.size(), (unsigned long)g_jobs.capacity(), (unsigned long)g_events.size(),
                  (unsigned long)g_events.capacity(), (unsigned long)g_buttons.size(),
                  (unsigned long)g_buttons.capacity(), (unsigned long)g_buttons.drops());
    }
    if (all || !std::strcmp(t, "qr")) {
      known = true;
      CardGeometry g = card_geometry(g_ctx, false);
      std::printf("qr: %s", g.qr_status == QrStatus::Ok ? "ok" : g.qr_status == QrStatus::Empty ? "not configured" : "too long");
      if (g.qr_status == QrStatus::Ok)
        std::printf(", version %d, %d px/module, %d px incl. quiet zone", g.qr_version, g.qr_scale, g.qr.w);
      std::printf("\r\n");
    }
    return known;
  }
  bool self_test() override {
    bool ok = true;
    for (int i = 0; i < fonts::count; ++i) {
      const bool f = font_compute_crc(*fonts::all[i]) == fonts::all[i]->crc;
      std::printf("font %-14s %s\r\n", fonts::all[i]->name, f ? "ok" : "CORRUPT");
      ok &= f;
    }
    const AssetPackInfo bi = asset_pack_validate(kBuiltinAssetPack, kBuiltinAssetPack_size);
    std::printf("built-in assets %s\r\n", asset_status_str(bi.status));
    ok &= bi.status == AssetStatus::Ok;
    const FieldDesc *bad = nullptr;
    const bool sv = settings_validate(g_staged, &bad);
    std::printf("settings %s%s\r\n", sv ? "ok" : "INVALID ", sv ? "" : bad->key);
    ok &= sv;
    std::printf("display event overflows %lu\r\n", (unsigned long)g_display.stats().event_overflows);
    ok &= g_display.stats().event_overflows == 0;
    return ok;
  }
  void request_sleep() override {
    if (g_sleep == SleepPhase::None) {
      std::printf("sleeping after the current refresh\r\n");
      g_sleep = SleepPhase::ShowSleepView;
      g_sched.invalidate();
    }
  }
  void request_reboot(bool bootsel) override {
    stdio_flush();
    sleep_ms(50);
    diag::reboot(diag::ResetKind::SoftReboot, bootsel);
  }
};

UsbCliHost g_cli_host;
Cli g_cli(g_cli_host);

void poll_cli() {
  char buf[64];
  size_t n = 0;
  int c;
  while (n < sizeof buf && (c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) buf[n++] = char(c);
  if (n) {
    g_cli.feed(buf, n);
    g_app.on_activity(now_ms());
  }
}

// -------------------------------------------------------------- sleep
[[noreturn]] void power_off() {
  std::printf("power off\r\n");
  stdio_flush();
  sleep_ms(20);
  board::led(0);
  cancel_repeating_timer(&g_button_timer);
  board::release_power_latch();
  // On battery the rail collapses once no button is held. If we are still
  // running, USB (or a held button) keeps us powered: emulate sleep. Wait for
  // every button to be released, then for a new press, and reboot so a wake
  // behaves like a battery cold boot (the press is captured at boot).
  uint32_t released_since = 0;
  bool armed = false;
  while (true) {
    diag::feed_watchdog();
    const uint32_t b = board::read_buttons() & ~(1u << int(Button::User));
    const uint32_t t = now_ms();
    if (!armed) {
      if (b) released_since = t;
      else if (t - released_since > 100) armed = true;
    } else if (b) {
      board::hold_power_latch();
      diag::reboot(diag::ResetKind::SleepWake);
    }
    sleep_ms(10);
  }
}

void poll_sleep() {
  if (g_sleep == SleepPhase::None) return;
  if (g_sleep == SleepPhase::ShowSleepView) {
    // Wait for the sleep view to be rendered and the panel to finish: the
    // image must be complete before power is removed.
    if (g_sched.settled() && !g_display.busy()) g_sleep = SleepPhase::PowerOff;
    return;
  }
  power_off();
}

}  // namespace

int main() {
  // Hold A + C while powering on / resetting to force safe mode.
  const uint32_t wake = board::wake_buttons();
  const bool force_safe = (wake & ((1u << int(Button::A)) | (1u << int(Button::C)))) ==
                          ((1u << int(Button::A)) | (1u << int(Button::C)));
  const diag::BootInfo &boot = diag::boot(force_safe);
  diag::paint_stacks();
  board::init();
  diag::start_watchdog();
  stdio_init_all();

  // Settings: defaults, then the newest valid flash record (not in safe mode).
  settings_defaults(&g_committed);
  g_single_core = boot.safe_mode;  // safe mode keeps everything on core 0
  static Rp2040Flash flash(false);
  static SettingsStore store(flash);
  g_flash = &flash;
  g_store = &store;
  // Always scan the slots so a commit targets the right one; in safe mode the
  // stored values are not applied.
  static Settings loaded;
  loaded = g_committed;
  store.load(&loaded);
  if (!boot.safe_mode) g_committed = loaded;
  g_staged = g_committed;
  g_single_core |= g_committed.prefs.single_core != 0;
  flash.set_core1_running(!g_single_core);
  g_core1_speed = g_staged.prefs.refresh_speed;

  select_assets();
  g_ctx.settings = &g_staged;
  g_ctx.info = &g_info_lines;
  static char reason[64];
  std::snprintf(reason, sizeof reason, "%s", force_safe ? "A+C held at boot" : "repeated crashes");
  g_ctx.recovery_reason = reason;

  g_vsys_mv = board::vsys_mv();
  g_usb = board::usb_powered();
  g_app.boot(app_config(boot.safe_mode), wake ? board::wake_button() : -1, now_ms());

  // The wake button is ignored until released (no accidental long-press).
  g_tracker.reset(wake);
  add_repeating_timer_ms(-int32_t(kSampleMs), button_timer_cb, nullptr, &g_button_timer);

  if (g_single_core) {
    g_display.start(g_staged.prefs.refresh_speed);
  } else {
    multicore_launch_core1(core1_main);
    const uint32_t t0 = now_ms();
    while (!g_core1_ready && now_ms() - t0 < 3000) sleep_ms(1);
  }
  g_sched.invalidate(false);

  uint32_t last_hb = 0, last_hb_change = now_ms(), last_sample = 0, last_info = 0;
  bool healthy = false;
  while (true) {
    const uint32_t t = now_ms();

    ButtonEvent ev;
    while (g_buttons.pop(&ev)) apply(g_app.on_button(ev));
    poll_cli();

    if (t - last_sample >= 2000) {
      last_sample = t;
      const bool usb = board::usb_powered();
      if (usb != g_usb) g_app.on_activity(t);  // plugging/unplugging restarts the idle timer
      g_usb = usb;
      g_vsys_mv = board::vsys_mv();
      apply(g_app.on_tick(t, !g_usb, g_sched.settled() && !g_display.busy()));
    }
    const Screen scr = g_app.view().screen;
    if ((scr == Screen::Info || scr == Screen::Recovery) && t - last_info >= 15000) {
      last_info = t;
      g_sched.invalidate();  // unchanged diagnostics are suppressed by hash
    }

    g_sched.poll(render_cb, nullptr, g_staged.prefs.refresh_speed,
                 g_staged.prefs.partial_refresh ? g_staged.prefs.max_partials : 0);
    if (g_single_core) {
      g_display.poll(t);
      g_sched.poll(render_cb, nullptr, g_staged.prefs.refresh_speed,
                   g_staged.prefs.partial_refresh ? g_staged.prefs.max_partials : 0);
    }
    board::led(g_display.busy() || g_sched.display_active() ? g_staged.prefs.led_level : 0);

    // Watchdog: feed only while the display context is alive (its heartbeat
    // advances every poll, including during long refreshes). In safe mode
    // keep USB alive regardless so the device can be repaired.
    const uint32_t hb = g_display.stats().heartbeat;
    if (hb != last_hb) { last_hb = hb; last_hb_change = t; }
    if (g_single_core || boot.safe_mode || t - last_hb_change < 3000) diag::feed_watchdog();
    if (!healthy && t > diag::kHealthyUptimeMs) { healthy = true; diag::mark_healthy(); }

    poll_sleep();
    best_effort_wfe_or_timeout(make_timeout_time_ms(g_single_core && g_display.busy() ? 5 : 10));
  }
}
