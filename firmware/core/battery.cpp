#include "battery.hpp"

namespace badge {

void BatteryMeter::configure(const BatteryThresholds &t) {
  t_ = t;
  // Defensive: keep the bars ordered even if given unsorted values.
  for (int i = 1; i < 4; ++i)
    if (t_.bar_mv[i] <= t_.bar_mv[i - 1]) t_.bar_mv[i] = uint16_t(t_.bar_mv[i - 1] + 1);
  if (t_.cal_permille < 900 || t_.cal_permille > 1100) t_.cal_permille = 1000;
}

uint16_t BatteryMeter::to_mv(const BatteryRaw &raw, uint16_t cal, uint16_t *vdd_mv) {
  if (raw.ref_counts == 0 || raw.ref_counts >= 4095) {
    if (vdd_mv) *vdd_mv = 0;
    return 0;
  }
  const uint32_t vdd = (1240u * 4095u + raw.ref_counts / 2) / raw.ref_counts;
  if (vdd_mv) *vdd_mv = uint16_t(vdd > 65535 ? 65535 : vdd);
  const uint64_t mv = (3ull * 1240ull * raw.bat_counts * cal + (uint64_t(raw.ref_counts) * 1000) / 2) /
                      (uint64_t(raw.ref_counts) * 1000);
  return uint16_t(mv > 65535 ? 65535 : mv);
}

uint8_t BatteryMeter::level_for(uint16_t mv) const {
  uint8_t n = 0;
  while (n < 4 && mv >= t_.bar_mv[n]) ++n;
  return n;
}

const BatteryState &BatteryMeter::update(const BatteryRaw &raw, bool usb) {
  ++st_.samples;
  uint16_t vdd = 0;
  const uint16_t mv = to_mv(raw, t_.cal_permille, &vdd);
  st_.last_mv = mv;
  st_.vdd_mv = vdd;
  if (usb) {
    // On USB the sense point is fed from VBUS, so it says nothing about the
    // cell; restart filtering when USB goes away.
    st_.display = PowerDisplay::Usb;
    seeded_ = false;
    return st_;
  }
  const bool valid = vdd >= kMinVddMv && vdd <= kMaxVddMv && mv >= kMinValidMv && mv <= kMaxValidMv;
  if (!valid) {
    ++st_.invalid;
    st_.display = PowerDisplay::Invalid;
    seeded_ = false;
    return st_;
  }
  if (!seeded_) {
    ema_x16_ = uint32_t(mv) * 16;
    seeded_ = true;
    st_.filtered_mv = mv;
    st_.bars = level_for(mv);
    st_.low = t_.low_mv && mv < t_.low_mv;
    st_.display = PowerDisplay::Battery;
    return st_;
  }
  // EMA, alpha = 1/4, in fixed point.
  ema_x16_ = ema_x16_ - ema_x16_ / 4 + uint32_t(mv) * 16 / 4;
  const uint16_t f = uint16_t((ema_x16_ + 8) / 16);
  st_.filtered_mv = f;
  const uint16_t h = t_.hyst_mv;
  const uint8_t cand = level_for(f);
  if (cand > st_.bars) {
    // Rise only as far as the voltage clears each threshold by `h`.
    while (st_.bars < 4 && f >= uint32_t(t_.bar_mv[st_.bars]) + h) ++st_.bars;
  } else if (cand < st_.bars) {
    while (st_.bars > 0 && uint32_t(f) + h < t_.bar_mv[st_.bars - 1]) --st_.bars;
  }
  if (t_.low_mv) {
    if (!st_.low && uint32_t(f) + h < t_.low_mv) st_.low = true;
    else if (st_.low && f >= uint32_t(t_.low_mv) + h) st_.low = false;
  } else {
    st_.low = false;
  }
  st_.display = PowerDisplay::Battery;
  return st_;
}

}  // namespace badge
