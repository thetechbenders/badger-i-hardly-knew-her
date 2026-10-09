// Which battery pack powers the badge, and how the meter reads it.
//
// The Badger 2040 runs from a single-cell LiPo or from a 2xAAA holder
// (target::kTwoAaaPackSupported); the Badger 2350 only from its LiPo. The
// packs need their own thresholds: a fresh alkaline pair (about 3.3 V) reads
// like a nearly empty LiPo, and a NiMH pair never reaches the LiPo's lowest
// bar.
//
// Settings: battery.pack (LiPo / 2xAAA), battery.aaa_cells (alkaline /
// NiMH; no reading tells them apart, so it is only ever chosen) and
// battery.auto. The LiPo keeps its configurable battery.*_mv thresholds; the
// 2xAAA chemistries use the presets in battery_pack.cpp. Hysteresis and
// calibration apply to every pack.
//
// Automatic switching (battery.auto): only a stable run of readings that no
// other pack can produce changes the pack. Inside the band where packs
// overlap, the remembered pack is kept:
//   every reading >= 3600 mV   LiPo    (above any 2xAAA pack)
//   every reading <= 2900 mV   2xAAA   (a LiPo is long past LOW there)
// Detection judges readings against the lowest floor of any pack, so a 2xAAA
// pack read under LiPo rules (shown as "?") still proves itself.
//
// A change, automatic or by hand on the Info screen, is saved on its own:
// save_battery_type() writes the committed record with only the pack fields
// changed, so unsaved CLI edits are never committed with it.
#pragma once

#include <cstdint>

#include "battery.hpp"
#include "battery_target.hpp"
#include "settings.hpp"

namespace badge {

class SettingsStore;

// Whether this target offers a choice of pack at all.
constexpr bool kBatteryPackChoice = target::kTwoAaaPackSupported;

// Values are stored (battery.pack, battery.aaa_cells): never renumber.
enum class BatteryPack : uint8_t { LiPo = 0, TwoAaa = 1 };
enum class AaaChemistry : uint8_t { Alkaline = 0, NiMH = 1 };

// What the Info screen steps through: pack and chemistry as one choice.
enum class BatteryType : uint8_t { LiPo = 0, AaaAlkaline, AaaNiMH, Count };

BatteryPack battery_pack(const Prefs &p);  // LiPo wherever 2xAAA is unsupported
BatteryType battery_type(const Prefs &p);
// The type `pack` means with the stored chemistry.
BatteryType battery_type_for_pack(BatteryPack pack, const Prefs &p);
// Choosing LiPo keeps the stored chemistry for the next 2xAAA pack.
void set_battery_type(Prefs *p, BatteryType type);
BatteryType step_battery_type(BatteryType type, int step);  // wraps around
const char *battery_type_name(BatteryType type);           // "LiPo", "AAA alkaline", "AAA NiMH"

// Meter thresholds and validity floor for the selected pack.
BatteryThresholds battery_thresholds(const Prefs &p);

// The pack may switch by itself: supported, battery.auto on, not safe mode.
bool battery_pack_auto_switch(const Prefs &p, bool safe_mode);
// The Info screen may change the pack: supported, not safe mode.
bool battery_type_selectable(bool safe_mode);

constexpr uint16_t kLipoProvenAtOrAboveMv = 3600;
constexpr uint16_t kTwoAaaProvenAtOrBelowMv = 2900;
// The pack a stable run proves, or `current` (no stable run, or overlap).
BatteryPack pack_proven_by(const StableReadingRun &run, BatteryPack current);

// Follows samples while awake; also drives the boot-time detection below.
class BatteryPackWatch {
 public:
  // One sample: returns the pack proven so far, else `current`. USB or an
  // implausible reading ends the run.
  BatteryPack feed(const BatteryMeter &meter, const BatterySample &sample, BatteryPack current);
  bool settled() const { return run_.stable(); }
  void restart() { run_.reset(); }

 private:
  StableReadingRun run_;
};

// Boot: sample until a stable run (InitialBatterySamplePolicy limits) and
// return the pack it proves. USB, an unsettled analog path or the overlap
// band keep `current`.
template <typename ReadFn, typename WaitFn>
BatteryPack detect_battery_pack(const BatteryMeter &meter, BatteryPack current, ReadFn read, WaitFn wait) {
  BatteryPackWatch watch;
  for (int attempt = 0; attempt < InitialBatterySamplePolicy::kMaxAttempts; ++attempt) {
    const BatterySample sample = read();
    if (sample.usb) return current;
    const BatteryPack proven = watch.feed(meter, sample, current);
    if (watch.settled()) return proven;
    if (attempt + 1 < InitialBatterySamplePolicy::kMaxAttempts) wait(InitialBatterySamplePolicy::kRetryDelayMs);
  }
  return current;
}

// Persist `type` alone: the committed record gains it and is written; the
// staged copy follows. On a failed write the committed record is unchanged,
// and so is the staged copy. No Settings copy is made (stack budget).
bool save_battery_type(SettingsStore &store, Settings *committed, Settings *staged, BatteryType type);

}  // namespace badge
