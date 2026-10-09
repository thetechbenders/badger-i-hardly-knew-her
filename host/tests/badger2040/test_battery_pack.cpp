// Badger 2040 only: the 2xAAA pack (alkaline or NiMH) besides the LiPo.
#include "battery_helpers.hpp"
#include "battery_pack.hpp"
#include "check.hpp"
#include "settings.hpp"

using namespace badge;
using battery_test::raw_for;

namespace {
Settings g_s;

BatteryState reading_as(BatteryType type, uint16_t mv) {
  settings_defaults(&g_s);
  set_battery_type(&g_s.prefs, type);
  BatteryMeter meter;
  meter.configure(battery_thresholds(g_s.prefs));
  return meter.update(raw_for(mv), false);
}
}  // namespace

TEST(badger2040_offers_the_two_aaa_pack) {
  CHECK(kBatteryPackChoice);
  settings_defaults(&g_s);
  CHECK(battery_type(g_s.prefs) == BatteryType::LiPo);  // existing badges keep reading a LiPo
  CHECK(g_s.prefs.battery_auto);
  CHECK(battery_pack_auto_switch(g_s.prefs, false));
  CHECK(battery_type_selectable(false));
  const FieldDesc &pack = *find_field("battery.pack");
  CHECK(settings_set(&g_s, pack, "1") == SetResult::Ok);
  CHECK(battery_type(g_s.prefs) == BatteryType::AaaAlkaline);
  CHECK(settings_set(&g_s, pack, "2") == SetResult::OutOfRange);
  CHECK(settings_set(&g_s, *find_field("battery.aaa_cells"), "1") == SetResult::Ok);
  CHECK(battery_type(g_s.prefs) == BatteryType::AaaNiMH);
  CHECK(settings_set(&g_s, *find_field("battery.aaa_cells"), "2") == SetResult::OutOfRange);
  CHECK(settings_validate(g_s, nullptr));
}

// Multimeter readings of real packs: a fresh alkaline pair at 3.3 to 3.4 V
// must read full, not empty, and a charged NiMH pair at 2.4 to 2.6 V must read
// as a battery with charge, not "?".
TEST(badger2040_two_aaa_presets) {
  struct Case {
    BatteryType type;
    uint16_t mv;
    uint8_t bars;
    bool low;
  } cases[] = {
      {BatteryType::AaaAlkaline, 3350, 4, false}, {BatteryType::AaaAlkaline, 2950, 4, false},
      {BatteryType::AaaAlkaline, 2800, 3, false}, {BatteryType::AaaAlkaline, 2600, 2, false},
      {BatteryType::AaaAlkaline, 2400, 1, false}, {BatteryType::AaaAlkaline, 2250, 0, false},
      {BatteryType::AaaAlkaline, 2100, 0, true},  {BatteryType::AaaNiMH, 2800, 4, false},
      {BatteryType::AaaNiMH, 2600, 3, false},     {BatteryType::AaaNiMH, 2450, 2, false},
      {BatteryType::AaaNiMH, 2350, 1, false},     {BatteryType::AaaNiMH, 2100, 0, true},
  };
  for (const Case &c : cases) {
    const BatteryState s = reading_as(c.type, c.mv);
    CHECK(s.display == PowerDisplay::Battery);
    CHECK_EQ(s.bars, c.bars);
    CHECK_EQ(s.low, c.low);
  }
  // A LiPo keeps its configurable thresholds and floor.
  CHECK(reading_as(BatteryType::LiPo, 3350).low);
  CHECK(reading_as(BatteryType::LiPo, 2450).display == PowerDisplay::Invalid);
}

TEST(badger2040_two_aaa_shares_hysteresis_and_calibration) {
  settings_defaults(&g_s);
  g_s.prefs.battery_hyst_mv = 25;
  g_s.prefs.battery_cal_permille = 1030;
  set_battery_type(&g_s.prefs, BatteryType::AaaNiMH);
  const BatteryThresholds t = battery_thresholds(g_s.prefs);
  CHECK_EQ(t.hyst_mv, 25);
  CHECK_EQ(t.cal_permille, 1030);
  CHECK_EQ(t.min_valid_mv, kTwoAaaMinValidMv);
  CHECK(t.low_mv <= t.bar_mv[0]);
  for (int bar = 1; bar < 4; ++bar) CHECK(t.bar_mv[bar] > t.bar_mv[bar - 1]);
}

// LiPo forgets nothing: the next 2xAAA pack, chosen by hand or detected,
// comes back with the chemistry chosen last.
TEST(badger2040_lipo_keeps_the_aaa_chemistry) {
  settings_defaults(&g_s);
  set_battery_type(&g_s.prefs, BatteryType::AaaNiMH);
  set_battery_type(&g_s.prefs, BatteryType::LiPo);
  CHECK(battery_type(g_s.prefs) == BatteryType::LiPo);
  CHECK(battery_type_for_pack(BatteryPack::TwoAaa, g_s.prefs) == BatteryType::AaaNiMH);
  CHECK(battery_type_for_pack(BatteryPack::LiPo, g_s.prefs) == BatteryType::LiPo);
}
