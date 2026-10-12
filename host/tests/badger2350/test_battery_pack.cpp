// Badger 2350 only: its charger means it only ever runs from a LiPo, so the
// 2xAAA choice of the Badger 2040 is refused everywhere.
#include "battery_pack.hpp"
#include "check.hpp"
#include "settings.hpp"

using namespace badge;

namespace {
Settings g_s;
}  // namespace

TEST(badger2350_battery_pack_is_always_lipo) {
  CHECK(!kBatteryPackChoice);
  settings_defaults(&g_s);
  CHECK(settings_set(&g_s, *find_field("battery.pack"), "1") == SetResult::OutOfRange);  // CLI: ERR
  CHECK(settings_set(&g_s, *find_field("battery.pack"), "0") == SetResult::Ok);
  set_battery_type(&g_s.prefs, BatteryType::AaaNiMH);
  CHECK(battery_type(g_s.prefs) == BatteryType::LiPo);
  CHECK_EQ(g_s.prefs.battery_pack, 0);
  g_s.prefs.battery_pack = 1;  // a record from elsewhere still reads as a LiPo
  CHECK(battery_pack(g_s.prefs) == BatteryPack::LiPo);
  CHECK(!battery_pack_auto_switch(g_s.prefs, false));
  CHECK(!battery_type_selectable(false));
  const BatteryThresholds t = battery_thresholds(g_s.prefs);
  CHECK_EQ(t.min_valid_mv, kLipoMinValidMv);
  CHECK_EQ(t.low_mv, g_s.prefs.battery_low_mv);
  CHECK_EQ(t.bar_mv[0], g_s.prefs.battery_bar_mv[0]);
}
