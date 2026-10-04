#include "battery.hpp"
#include "check.hpp"
#include "renderer.hpp"

using namespace badge;

namespace {
// Counts that produce `mv` at a nominal 3.30 V supply (inverse of to_mv()).
BatteryRaw raw_for(uint32_t mv, uint32_t vdd_mv = 3300) {
  BatteryRaw r;
  r.ref_counts = uint16_t((1240u * 4095u + vdd_mv / 2) / vdd_mv);
  r.bat_counts = uint16_t((mv * r.ref_counts + 1860) / 3720);
  return r;
}
}  // namespace

TEST(battery_conversion_matches_upstream_formula) {
  // Pimoroni: vdd = 1.24 * 65535 / ref ; vbat = bat / 65535 * 3 * vdd (16-bit
  // MicroPython scale); the 12-bit native reading gives the same result.
  uint16_t vdd;
  CHECK_EQ(BatteryMeter::to_mv({1539, 1366}, 1000, &vdd), 3302);  // 3 * 1240 * 1366 / 1539
  CHECK(vdd >= 3295 && vdd <= 3305);
  // Supply sags to 3.0 V: the reference corrects it.
  const BatteryRaw sag = raw_for(3700, 3000);
  const uint16_t mv = BatteryMeter::to_mv(sag, 1000, &vdd);
  CHECK(mv >= 3690 && mv <= 3710);
  CHECK(vdd >= 2990 && vdd <= 3010);
  CHECK_EQ(BatteryMeter::to_mv({0, 1000}, 1000, &vdd), 0);  // no reference
  CHECK(BatteryMeter::to_mv(raw_for(4000), 1020, nullptr) >= 4075);  // calibration scales
}

// Badger 2350: 1.1 V reference (SENSE_1V1), 1/2 divider (VBAT_SENSE).
// BadgeWare: vbat = 2 * 1.1 * raw_vbat / raw_1v1 (16-bit raw values; the
// 12-bit native reading gives the same ratio).
TEST(battery_conversion_badger2350_circuit) {
  const BatteryCircuit c{1100, 2};
  uint16_t vdd;
  // 3.3 V supply: ref = 1.1 / 3.3 * 4095 = 1365; a 3.80 V cell reads 1.90 V = 2358.
  CHECK_EQ(BatteryMeter::to_mv({1365, 2358}, 1000, &vdd, c), 3800);  // 2 * 1100 * 2358 / 1365 = 3800.4
  CHECK(vdd >= 3295 && vdd <= 3305);
  // BadgeWare's float formula on the same counts scaled to 16 bits.
  const double badgeware = 2 * 1.1 * (2358 * 16.0) / (1365 * 16.0);
  CHECK(BatteryMeter::to_mv({1365, 2358}, 1000, nullptr, c) == uint16_t(badgeware * 1000 + 0.5));
  // Supply sag is corrected by the reference here too.
  CHECK(BatteryMeter::to_mv({1501, 2358}, 1000, &vdd, c) >= 3450 &&
        BatteryMeter::to_mv({1501, 2358}, 1000, &vdd, c) <= 3460);
  CHECK(vdd >= 2995 && vdd <= 3005);
  // The meter uses its circuit.
  BatteryMeter m(c);
  m.configure(BatteryThresholds{});
  CHECK_EQ(m.update({1365, 2358}, false).last_mv, 3800);
  CHECK(m.state().display == PowerDisplay::Battery);
  CHECK_EQ(m.circuit().ref_mv, 1100);
  CHECK_EQ(BatteryMeter().circuit().ref_mv, 1240);  // default: the Badger 2040's
}

TEST(battery_bars_and_low_for_lipo_defaults) {
  struct { uint16_t mv; uint8_t bars; bool low; } cases[] = {
      {4200, 4, false}, {3960, 4, false}, {3900, 3, false}, {3750, 2, false},
      {3650, 1, false}, {3550, 0, false}, {3400, 0, true},
  };
  for (auto &c : cases) {
    BatteryMeter m;
    m.configure(BatteryThresholds{});
    const BatteryState &s = m.update(raw_for(c.mv), false);
    CHECK(s.display == PowerDisplay::Battery);
    CHECK_EQ(s.bars, c.bars);
    CHECK_EQ(s.low, c.low);
  }
}

TEST(battery_hysteresis_prevents_flicker) {
  BatteryMeter m;
  m.configure(BatteryThresholds{});  // bar3 at 3800, hyst 40
  m.update(raw_for(3810), false);
  CHECK_EQ(m.state().bars, 3);
  // Noisy readings straddling 3800 must not toggle the icon.
  int changes = 0;
  uint8_t last = m.state().bars;
  for (int i = 0; i < 200; ++i) {
    m.update(raw_for(i % 2 ? 3780 : 3815), false);
    if (m.state().bars != last) { ++changes; last = m.state().bars; }
  }
  CHECK_EQ(changes, 0);
  // A real drop well below the threshold does register.
  for (int i = 0; i < 20; ++i) m.update(raw_for(3740), false);
  CHECK_EQ(m.state().bars, 2);
  // Coming back needs threshold + hysteresis.
  for (int i = 0; i < 20; ++i) m.update(raw_for(3825), false);
  CHECK_EQ(m.state().bars, 2);
  for (int i = 0; i < 20; ++i) m.update(raw_for(3850), false);
  CHECK_EQ(m.state().bars, 3);
}

TEST(battery_filter_rejects_single_outlier) {
  BatteryMeter m;
  m.configure(BatteryThresholds{});
  for (int i = 0; i < 10; ++i) m.update(raw_for(3900), false);
  m.update(raw_for(3500), false);  // one sag sample (e.g. load spike)
  CHECK_EQ(m.state().bars, 3);
  CHECK(!m.state().low);
  CHECK(m.state().filtered_mv > 3750);
}

TEST(battery_usb_and_invalid_states) {
  BatteryMeter m;
  m.configure(BatteryThresholds{});
  CHECK(m.state().display == PowerDisplay::Unknown);
  CHECK(m.update(raw_for(4900), true).display == PowerDisplay::Usb);  // USB: never bars, never "charging"
  CHECK(m.update({0, 0}, false).display == PowerDisplay::Invalid);       // reference missing
  CHECK(m.update(raw_for(1200), false).display == PowerDisplay::Invalid);  // floating / no cell
  CHECK(m.update(raw_for(5200), false).display == PowerDisplay::Invalid);  // above any LiPo
  CHECK(m.update(raw_for(3700, 1500), false).display == PowerDisplay::Invalid);  // implausible supply
  CHECK(m.state().invalid >= 4u);
  CHECK(m.update(raw_for(3900), false).display == PowerDisplay::Battery);  // recovers, re-seeded
  CHECK_EQ(m.state().bars, 3);
}

TEST(battery_wake_first_frame_retries_transient_invalid_sample) {
  // Normal operation has a valid reading before the badge powers down.
  BatteryMeter running;
  running.configure(BatteryThresholds{});
  CHECK(running.update(raw_for(3900), false).display == PowerDisplay::Battery);

  App app;
  AppConfig cfg;
  cfg.qr_configured = true;
  cfg.sleep_timeout_s = 60;
  app.boot(cfg, -1, 0);
  CHECK_EQ(app.on_button({Button::Down, Gesture::Long, 1000}), kActSleep);
  CHECK(app.sleeping());

  // A real Classic battery wake is a cold boot. Reproduce the observed
  // sequence: the first ADC result is invalid, then the settled reading is
  // valid before the first post-wake frame is constructed.
  BatteryMeter wake;
  wake.configure(BatteryThresholds{});
  int reads = 0, waits = 0;
  const BatteryState &first = sample_initial_battery(
      wake,
      [&]() {
        ++reads;
        return reads == 1 ? BatterySample{{0, 0}, false}
                          : BatterySample{raw_for(3900), false};
      },
      [&](uint32_t) { ++waits; });
  CHECK(first.display == PowerDisplay::Battery);
  CHECK_EQ(reads, 2);
  CHECK_EQ(waits, 1);

  app.boot(cfg, int(Button::B), 0);
  CHECK(app.view().screen == Screen::Card);
  Settings settings;
  settings_defaults(&settings);
  RenderContext ctx;
  ctx.settings = &settings;
  ctx.status.battery = first;
  Framebuffer fb;
  render(fb, app.view(), ctx);

  const Rect status = status_rect(app.view(), ctx);
  bool status_drawn = false;
  for (int y = status.y; y < status.bottom(); ++y)
    for (int x = status.x; x < status.right(); ++x)
      status_drawn |= fb.get(x, y) == Ink::Black;
  CHECK(status_drawn);
}

TEST(battery_initial_sample_preserves_persistent_failure) {
  BatteryMeter m;
  m.configure(BatteryThresholds{});
  int reads = 0, waits = 0;
  const BatteryState &s = sample_initial_battery(
      m,
      [&]() {
        ++reads;
        return BatterySample{{0, 0}, false};
      },
      [&](uint32_t) { ++waits; });

  CHECK(s.display == PowerDisplay::Invalid);
  CHECK_EQ(reads, InitialBatterySamplePolicy::kMaxAttempts);
  CHECK_EQ(waits, InitialBatterySamplePolicy::kMaxAttempts - 1);
}

TEST(battery_unsorted_thresholds_are_sanitised) {
  BatteryThresholds t;
  t.bar_mv[2] = 3600;  // below bar2
  BatteryMeter m;
  m.configure(t);
  const BatteryState &s = m.update(raw_for(4100), false);
  CHECK_EQ(s.bars, 4);
}
