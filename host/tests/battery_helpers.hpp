// Shared by the battery meter and battery pack tests.
#pragma once

#include <cstdint>

#include "battery.hpp"

namespace battery_test {

// Counts that produce `mv` on the Badger 2040 circuit (1.24 V reference, 1/3
// divider) at the given supply: the inverse of BatteryMeter::to_mv().
inline badge::BatteryRaw raw_for(uint32_t mv, uint32_t vdd_mv = 3300) {
  badge::BatteryRaw r;
  r.ref_counts = uint16_t((1240u * 4095u + vdd_mv / 2) / vdd_mv);
  r.bat_counts = uint16_t((mv * r.ref_counts + 1860) / 3720);
  return r;
}

}  // namespace battery_test
