#include "battery_pack.hpp"

#include "settings_store.hpp"

namespace badge {

namespace {

// What each BatteryType stores. LiPo leaves battery.aaa_cells alone.
struct BatteryTypeChoice {
  BatteryPack pack;
  AaaChemistry chemistry;
  bool sets_chemistry;
  const char *name;
};
constexpr BatteryTypeChoice kBatteryTypeChoices[] = {
    {BatteryPack::LiPo, AaaChemistry::Alkaline, false, "LiPo"},
    {BatteryPack::TwoAaa, AaaChemistry::Alkaline, true, "AAA alkaline"},
    {BatteryPack::TwoAaa, AaaChemistry::NiMH, true, "AAA NiMH"},
};
static_assert(sizeof kBatteryTypeChoices / sizeof kBatteryTypeChoices[0] == size_t(BatteryType::Count),
              "one choice per BatteryType");

// 2xAAA presets (resting voltage of the pair, before load). First estimates:
// check them against a multimeter as for the LiPo (docs/BATTERY.md).
//   alkaline  falls steadily from about 3.2 V fresh to 2.0 V spent
//   NiMH      about 2.8 V off the charger, then flat near 2.4 to 2.5 V for
//             most of its charge, so its bars are coarse
struct TwoAaaLevels {
  uint16_t low_mv;
  uint16_t bar_mv[4];
};
constexpr TwoAaaLevels kAlkalineLevels{2200, {2300, 2500, 2700, 2900}};
constexpr TwoAaaLevels kNiMHLevels{2200, {2300, 2400, 2500, 2650}};

AaaChemistry stored_chemistry(const Prefs &p) {
  return p.battery_aaa_cells == uint8_t(AaaChemistry::NiMH) ? AaaChemistry::NiMH : AaaChemistry::Alkaline;
}

const BatteryTypeChoice &choice_for(BatteryType type) {
  const size_t index = size_t(type) < size_t(BatteryType::Count) ? size_t(type) : 0;
  return kBatteryTypeChoices[index];
}

}  // namespace

BatteryPack battery_pack(const Prefs &p) {
  if (!kBatteryPackChoice) return BatteryPack::LiPo;
  return p.battery_pack == uint8_t(BatteryPack::TwoAaa) ? BatteryPack::TwoAaa : BatteryPack::LiPo;
}

BatteryType battery_type_for_pack(BatteryPack pack, const Prefs &p) {
  if (pack == BatteryPack::LiPo) return BatteryType::LiPo;
  return stored_chemistry(p) == AaaChemistry::NiMH ? BatteryType::AaaNiMH : BatteryType::AaaAlkaline;
}

BatteryType battery_type(const Prefs &p) { return battery_type_for_pack(battery_pack(p), p); }

void set_battery_type(Prefs *p, BatteryType type) {
  const BatteryTypeChoice &choice = choice_for(kBatteryPackChoice ? type : BatteryType::LiPo);
  p->battery_pack = uint8_t(choice.pack);
  if (choice.sets_chemistry) p->battery_aaa_cells = uint8_t(choice.chemistry);
}

BatteryType step_battery_type(BatteryType type, int step) {
  const int count = int(BatteryType::Count);
  return BatteryType(((int(type) + step) % count + count) % count);
}

const char *battery_type_name(BatteryType type) { return choice_for(type).name; }

BatteryThresholds battery_thresholds(const Prefs &p) {
  BatteryThresholds t;
  t.hyst_mv = p.battery_hyst_mv;
  t.cal_permille = p.battery_cal_permille;
  if (battery_pack(p) == BatteryPack::LiPo) {
    t.low_mv = p.battery_low_mv;
    for (int bar = 0; bar < 4; ++bar) t.bar_mv[bar] = p.battery_bar_mv[bar];
    t.min_valid_mv = kLipoMinValidMv;
    return t;
  }
  const TwoAaaLevels &levels = stored_chemistry(p) == AaaChemistry::NiMH ? kNiMHLevels : kAlkalineLevels;
  t.low_mv = levels.low_mv;
  for (int bar = 0; bar < 4; ++bar) t.bar_mv[bar] = levels.bar_mv[bar];
  t.min_valid_mv = kTwoAaaMinValidMv;
  return t;
}

bool battery_pack_auto_switch(const Prefs &p, bool safe_mode) {
  return kBatteryPackChoice && p.battery_auto && !safe_mode;
}

bool battery_type_selectable(bool safe_mode) { return kBatteryPackChoice && !safe_mode; }

BatteryPack pack_proven_by(const StableReadingRun &run, BatteryPack current) {
  if (!run.stable()) return current;
  if (run.lowest_mv() >= kLipoProvenAtOrAboveMv) return BatteryPack::LiPo;
  if (run.highest_mv() <= kTwoAaaProvenAtOrBelowMv) return BatteryPack::TwoAaa;
  return current;
}

BatteryPack BatteryPackWatch::feed(const BatteryMeter &meter, const BatterySample &sample, BatteryPack current) {
  if (sample.usb) {  // the sense point reflects VBUS, not the cell
    run_.reset();
    return current;
  }
  const BatteryMeasurement measurement = meter.inspect(sample.raw, kTwoAaaMinValidMv);
  if (!measurement.valid) {
    run_.reset();
    return current;
  }
  run_.add(measurement.mv);
  return pack_proven_by(run_, current);
}

bool save_battery_type(SettingsStore &store, Settings *committed, Settings *staged, BatteryType type) {
  const BatteryType saved_type = battery_type(committed->prefs);
  const uint8_t saved_chemistry = committed->prefs.battery_aaa_cells;
  set_battery_type(&committed->prefs, type);
  const bool unchanged = battery_type(committed->prefs) == saved_type &&
                         committed->prefs.battery_aaa_cells == saved_chemistry;
  if (!unchanged && !store.commit(*committed)) {
    set_battery_type(&committed->prefs, saved_type);
    committed->prefs.battery_aaa_cells = saved_chemistry;
    return false;
  }
  set_battery_type(&staged->prefs, type);
  return true;
}

}  // namespace badge
