#include "battery.hpp"
#include "check.hpp"

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

TEST(battery_unsorted_thresholds_are_sanitised) {
  BatteryThresholds t;
  t.bar_mv[2] = 3600;  // below bar2
  BatteryMeter m;
  m.configure(t);
  const BatteryState &s = m.update(raw_for(4100), false);
  CHECK_EQ(s.bars, 4);
}
