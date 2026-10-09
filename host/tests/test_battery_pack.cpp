// Battery pack detection rules (battery_pack.hpp). Whether a target offers
// the 2xAAA pack at all is tested per target (tests/<target>/).
#include <initializer_list>

#include "battery_helpers.hpp"
#include "battery_pack.hpp"
#include "check.hpp"
#include "settings.hpp"

using namespace badge;
using battery_test::raw_for;

namespace {
StableReadingRun run_of(uint16_t mv) {
  StableReadingRun run;
  for (int sample = 0; sample < InitialBatterySamplePolicy::kStableSamples; ++sample) run.add(mv);
  return run;
}

BatteryMeter lipo_meter() {
  BatteryMeter meter;
  meter.configure(BatteryThresholds{});
  return meter;
}

// Scripted samples for detect_battery_pack(), repeated in a cycle; counts
// reads and waits.
struct SampleScript {
  const uint16_t *mv;
  int count;
  bool usb = false;
  int reads = 0, waits = 0;
  BatterySample next() {
    const uint16_t reading = mv[reads % count];
    ++reads;
    return BatterySample{raw_for(reading), usb};
  }
};

BatteryPack detect(SampleScript &script, BatteryPack current) {
  return detect_battery_pack(lipo_meter(), current, [&] { return script.next(); },
                             [&](uint32_t) { ++script.waits; });
}
}  // namespace

// Only readings no other pack can produce move the pack; the band where a
// fresh alkaline pair and a nearly empty LiPo overlap keeps the current one.
TEST(battery_pack_proven_only_outside_the_overlap) {
  for (BatteryPack current : {BatteryPack::LiPo, BatteryPack::TwoAaa}) {
    CHECK(pack_proven_by(run_of(kLipoProvenAtOrAboveMv), current) == BatteryPack::LiPo);
    CHECK(pack_proven_by(run_of(4150), current) == BatteryPack::LiPo);
    CHECK(pack_proven_by(run_of(kTwoAaaProvenAtOrBelowMv), current) == BatteryPack::TwoAaa);
    CHECK(pack_proven_by(run_of(2400), current) == BatteryPack::TwoAaa);
    for (uint16_t overlap : {uint16_t(kTwoAaaProvenAtOrBelowMv + 1), uint16_t(3300), uint16_t(3450),
                             uint16_t(kLipoProvenAtOrAboveMv - 1)})
      CHECK(pack_proven_by(run_of(overlap), current) == current);
    StableReadingRun unsettled;
    unsettled.add(4100);
    CHECK(pack_proven_by(unsettled, current) == current);
  }
  // Every reading of the run must clear the bound, not just the newest.
  StableReadingRun straddling;
  for (uint16_t mv : {uint16_t(3580), uint16_t(3610), uint16_t(3620)}) straddling.add(mv);
  CHECK(straddling.stable());
  CHECK(pack_proven_by(straddling, BatteryPack::TwoAaa) == BatteryPack::TwoAaa);
}

// A NiMH pair under LiPo rules reads below the LiPo floor ("?"), yet still
// proves itself; USB and implausible readings end the run.
TEST(battery_pack_watch_follows_stable_plausible_readings) {
  const BatteryMeter meter = lipo_meter();
  CHECK(!meter.inspect(raw_for(2450)).valid);
  BatteryPackWatch watch;
  CHECK(watch.feed(meter, {raw_for(2450), false}, BatteryPack::LiPo) == BatteryPack::LiPo);
  CHECK(watch.feed(meter, {raw_for(2460), false}, BatteryPack::LiPo) == BatteryPack::LiPo);
  CHECK(watch.feed(meter, {raw_for(2450), false}, BatteryPack::LiPo) == BatteryPack::TwoAaa);
  CHECK(watch.settled());

  watch.restart();
  watch.feed(meter, {raw_for(2450), false}, BatteryPack::LiPo);
  watch.feed(meter, {raw_for(2450), false}, BatteryPack::LiPo);
  CHECK(watch.feed(meter, {raw_for(5000), true}, BatteryPack::LiPo) == BatteryPack::LiPo);  // USB
  CHECK(!watch.settled());
  watch.feed(meter, {raw_for(2450), false}, BatteryPack::LiPo);
  watch.feed(meter, {raw_for(2450), false}, BatteryPack::LiPo);
  CHECK(watch.feed(meter, {raw_for(1500), false}, BatteryPack::LiPo) == BatteryPack::LiPo);  // implausible
  CHECK(!watch.settled());
}

TEST(battery_pack_detected_at_boot) {
  const uint16_t nimh[] = {2480, 2470, 2475};
  SampleScript swapped{nimh, 3};
  CHECK(detect(swapped, BatteryPack::LiPo) == BatteryPack::TwoAaa);
  CHECK_EQ(swapped.reads, 3);  // stops as soon as three readings agree
  CHECK_EQ(swapped.waits, 2);

  const uint16_t lipo[] = {3900};
  SampleScript charged{lipo, 1};
  CHECK(detect(charged, BatteryPack::TwoAaa) == BatteryPack::LiPo);

  const uint16_t fresh_alkaline[] = {3350};
  SampleScript ambiguous{fresh_alkaline, 1};
  CHECK(detect(ambiguous, BatteryPack::LiPo) == BatteryPack::LiPo);  // the overlap keeps the pack
  CHECK_EQ(ambiguous.reads, 3);

  SampleScript on_usb{lipo, 1};
  on_usb.usb = true;
  CHECK(detect(on_usb, BatteryPack::TwoAaa) == BatteryPack::TwoAaa);
  CHECK_EQ(on_usb.reads, 1);

  const uint16_t unsettled[] = {2400, 3900};  // never three in a row
  SampleScript noisy{unsettled, 2};
  CHECK(detect(noisy, BatteryPack::TwoAaa) == BatteryPack::TwoAaa);
  CHECK_EQ(noisy.reads, InitialBatterySamplePolicy::kMaxAttempts);
  CHECK_EQ(noisy.waits, InitialBatterySamplePolicy::kMaxAttempts - 1);
}

TEST(battery_pack_nothing_switches_in_safe_mode) {
  Settings s;
  settings_defaults(&s);
  CHECK(!battery_pack_auto_switch(s.prefs, true));
  CHECK(!battery_type_selectable(true));
  s.prefs.battery_auto = 0;
  CHECK(!battery_pack_auto_switch(s.prefs, false));
}

TEST(battery_type_steps_wrap_both_ways) {
  CHECK(step_battery_type(BatteryType::LiPo, 1) == BatteryType::AaaAlkaline);
  CHECK(step_battery_type(BatteryType::AaaAlkaline, 1) == BatteryType::AaaNiMH);
  CHECK(step_battery_type(BatteryType::AaaNiMH, 1) == BatteryType::LiPo);
  CHECK(step_battery_type(BatteryType::LiPo, -1) == BatteryType::AaaNiMH);
  CHECK_STR(battery_type_name(BatteryType::LiPo), "LiPo");
  CHECK_STR(battery_type_name(BatteryType::AaaAlkaline), "AAA alkaline");
  CHECK_STR(battery_type_name(BatteryType::AaaNiMH), "AAA NiMH");
}
