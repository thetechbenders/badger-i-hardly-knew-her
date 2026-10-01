// Battery meter for a single-cell LiPo on the original Badger 2040.
//
// Input: one ADC measurement = raw counts of the 1.24 V reference (ADC2) and
// of the battery-sense divider (ADC3, gain 1/3), taken by the platform.
// Output: a stable display state for a four-bar icon.
//
//   vdd  = 1.24 V * 4095 / ref_counts          (actual ADC supply)
//   vbat = bat_counts / 4095 * 3 * vdd * cal   (= 3 * 1240 mV * bat / ref * cal)
//
// Filtering: exponential moving average (alpha 1/4) seeded by the first valid
// reading. Hysteresis: the bar count only moves up once the voltage exceeds
// the next threshold by `hyst`, and only moves down once it falls `hyst`
// below the current level's threshold. LOW uses the same rule.
// Readings are rejected as invalid when the reference or the result is out of
// a plausible range (sensor fault, missing reference, floating input).
// Voltage-to-charge mapping is approximate; the icon never claims precision
// and never shows "charging" (the board has no charger).
#pragma once

#include <cstdint>

namespace badge {

struct BatteryThresholds {
  uint16_t low_mv = 3500;  // 0 disables LOW
  uint16_t bar_mv[4] = {3600, 3700, 3800, 3950};
  uint16_t hyst_mv = 40;
  uint16_t cal_permille = 1000;
};

struct BatteryRaw {
  uint16_t ref_counts;  // ADC2, 12-bit average
  uint16_t bat_counts;  // ADC3, 12-bit average
};

enum class PowerDisplay : uint8_t {
  Unknown = 0,  // no measurement yet: draw nothing
  Usb,          // VBUS present: show "USB" (never "charging")
  Battery,      // bars (0..4), possibly LOW
  Invalid,      // implausible reading: show "?"
};

struct BatteryState {
  PowerDisplay display = PowerDisplay::Unknown;
  uint8_t bars = 0;       // 0..4
  bool low = false;
  uint16_t last_mv = 0;   // last single (unfiltered) reading, calibrated
  uint16_t filtered_mv = 0;
  uint16_t vdd_mv = 0;    // derived ADC supply voltage
  uint32_t samples = 0, invalid = 0;
};

class BatteryMeter {
 public:
  static constexpr uint16_t kMinValidMv = 2500;  // below: reading, not battery, is wrong
  static constexpr uint16_t kMaxValidMv = 4600;  // above a LiPo's 4.2 V + margin
  static constexpr uint16_t kMinVddMv = 1800, kMaxVddMv = 3700;

  void configure(const BatteryThresholds &t);
  // Feed one measurement. `usb` = VBUS detected (reading then reflects USB).
  const BatteryState &update(const BatteryRaw &raw, bool usb);
  const BatteryState &state() const { return st_; }
  // Pure conversion (exposed for tests and diagnostics); 0 if invalid.
  static uint16_t to_mv(const BatteryRaw &raw, uint16_t cal_permille, uint16_t *vdd_mv);

 private:
  uint8_t level_for(uint16_t mv) const;
  BatteryThresholds t_;
  BatteryState st_;
  bool seeded_ = false;
  uint32_t ema_x16_ = 0;  // filtered value * 16
};

}  // namespace badge
