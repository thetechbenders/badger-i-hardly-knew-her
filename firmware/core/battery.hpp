// Battery meter for a single-cell LiPo.
//
// Input: one ADC measurement = raw counts of a fixed voltage reference and
// of the battery-sense divider, taken by the platform, plus the circuit
// (BatteryCircuit: reference voltage and divider ratio, from the board):
//   Badger 2040  1.24 V reference (ADC2), divider 1/3 (ADC3)
//   Badger 2350  1.1 V reference (SENSE_1V1, ADC2), divider 1/2 (VBAT_SENSE, ADC0)
// Both ADCs run from the same supply, so the reference recovers it:
//
//   vdd  = ref_mv * 4095 / ref_counts                  (actual ADC supply)
//   vbat = bat_counts / 4095 * divider * vdd * cal     (= divider * ref_mv * bat / ref * cal)
//
// Output: a stable display state for a four-bar icon.
//
// Filtering: exponential moving average (alpha 1/4) seeded by the first valid
// reading. Hysteresis: the bar count only moves up once the voltage exceeds
// the next threshold by `hyst`, and only moves down once it falls `hyst`
// below the current level's threshold. LOW uses the same rule.
// Readings are rejected as invalid when the reference or the result is out of
// a plausible range (sensor fault, missing reference, floating input).
// Voltage-to-charge mapping is approximate; the icon never claims precision
// and never shows "charging": the Badger 2040 has no charger, and the Badger
// 2350's charge-status line sits on its wireless chip, which this firmware
// does not run. With VBUS present the state is "USB", nothing more.
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
  uint16_t ref_counts;  // reference, 12-bit average
  uint16_t bat_counts;  // battery-sense divider, 12-bit average
};

struct BatteryCircuit {
  uint16_t ref_mv;  // reference voltage
  uint8_t divider;  // vbat = divider * sense voltage
};
constexpr BatteryCircuit kBadger2040Battery{1240, 3};

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

struct BatterySample {
  BatteryRaw raw;
  bool usb = false;
};

class BatteryMeter {
 public:
  static constexpr uint16_t kMinValidMv = 2500;  // below: reading, not battery, is wrong
  static constexpr uint16_t kMaxValidMv = 4600;  // above a LiPo's 4.2 V + margin
  static constexpr uint16_t kMinVddMv = 1800, kMaxVddMv = 3700;

  explicit BatteryMeter(BatteryCircuit c = kBadger2040Battery) : circuit_(c) {}
  void configure(const BatteryThresholds &t);
  // Feed one measurement. `usb` = VBUS detected (reading then reflects USB).
  const BatteryState &update(const BatteryRaw &raw, bool usb);
  const BatteryState &state() const { return st_; }
  // Pure conversion (exposed for tests and diagnostics); 0 if invalid.
  static uint16_t to_mv(const BatteryRaw &raw, uint16_t cal_permille, uint16_t *vdd_mv,
                        BatteryCircuit c = kBadger2040Battery);
  BatteryCircuit circuit() const { return circuit_; }

 private:
  uint8_t level_for(uint16_t mv) const;
  BatteryCircuit circuit_;
  BatteryThresholds t_;
  BatteryState st_;
  bool seeded_ = false;
  uint32_t ema_x16_ = 0;  // filtered value * 16
};

// Initial boot/wake sampling policy. A cold power-up can briefly produce an
// implausible ADC result before the board's power/reference path has settled.
// Retry only an Invalid result for a short bounded window; a valid battery or
// USB reading is accepted immediately, and a persistent failure remains
// Invalid so the UI still truthfully shows "?".
struct InitialBatterySamplePolicy {
  static constexpr int kMaxAttempts = 5;
  static constexpr uint32_t kRetryDelayMs = 25;
};

template <typename ReadFn, typename WaitFn>
const BatteryState &sample_initial_battery(BatteryMeter &meter, ReadFn read, WaitFn wait) {
  const BatteryState *state = &meter.state();
  for (int attempt = 0; attempt < InitialBatterySamplePolicy::kMaxAttempts; ++attempt) {
    const BatterySample sample = read();
    state = &meter.update(sample.raw, sample.usb);
    if (state->display != PowerDisplay::Invalid) break;
    if (attempt + 1 < InitialBatterySamplePolicy::kMaxAttempts)
      wait(InitialBatterySamplePolicy::kRetryDelayMs);
  }
  return *state;
}

}  // namespace badge
