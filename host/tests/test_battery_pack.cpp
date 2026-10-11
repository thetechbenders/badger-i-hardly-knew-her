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

// Only a LiPo is ever proven, by readings no 2xAAA pack reaches; anything
// lower keeps the current pack, so 2xAAA is always chosen by hand.
TEST(battery_pack_only_a_lipo_is_ever_proven) {
  for (BatteryPack current : {BatteryPack::LiPo, BatteryPack::TwoAaa}) {
    CHECK(pack_proven_by(run_of(kLipoProvenAtOrAboveMv), current) == BatteryPack::LiPo);
    CHECK(pack_proven_by(run_of(4150), current) == BatteryPack::LiPo);
    for (uint16_t below : {uint16_t(kLipoProvenAtOrAboveMv - 1), uint16_t(3300), uint16_t(2900), uint16_t(2400)})
      CHECK(pack_proven_by(run_of(below), current) == current);
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

// A deeply discharged LiPo reads like a 2xAAA pair (valid to the LiPo meter
// down to 2.5 V). It must stay a LiPo: as AAA it would show healthy bars.
TEST(battery_pack_dying_lipo_is_never_taken_for_aaa) {
  const BatteryMeter meter = lipo_meter();
  BatteryPackWatch watch;
  for (uint16_t mv : {uint16_t(2900), uint16_t(2700), uint16_t(2600)}) {
    CHECK(meter.inspect(raw_for(mv)).valid);
    for (int sample = 0; sample < 4; ++sample)
      CHECK(watch.feed(meter, {raw_for(mv), false}, BatteryPack::LiPo) == BatteryPack::LiPo);
  }
  const uint16_t dying[] = {2650};
  SampleScript at_boot{dying, 1};
  CHECK(detect(at_boot, BatteryPack::LiPo) == BatteryPack::LiPo);
}

// A charged LiPo fitted while 2xAAA is selected proves itself; USB and
// invalid readings end the run.
TEST(battery_pack_watch_follows_stable_valid_readings) {
  BatteryMeter meter;
  BatteryThresholds pair;
  pair.min_valid_mv = kTwoAaaMinValidMv;
  meter.configure(pair);
  BatteryPackWatch watch;
  CHECK(watch.feed(meter, {raw_for(3900), false}, BatteryPack::TwoAaa) == BatteryPack::TwoAaa);
  CHECK(watch.feed(meter, {raw_for(3910), false}, BatteryPack::TwoAaa) == BatteryPack::TwoAaa);
  CHECK(watch.feed(meter, {raw_for(3900), false}, BatteryPack::TwoAaa) == BatteryPack::LiPo);
  CHECK(watch.settled());

  watch.restart();
  watch.feed(meter, {raw_for(3900), false}, BatteryPack::TwoAaa);
  watch.feed(meter, {raw_for(3900), false}, BatteryPack::TwoAaa);
  CHECK(watch.feed(meter, {raw_for(5000), true}, BatteryPack::TwoAaa) == BatteryPack::TwoAaa);  // USB
  CHECK(!watch.settled());
  watch.feed(meter, {raw_for(3900), false}, BatteryPack::TwoAaa);
  watch.feed(meter, {raw_for(3900), false}, BatteryPack::TwoAaa);
  CHECK(watch.feed(meter, {raw_for(1500), false}, BatteryPack::TwoAaa) == BatteryPack::TwoAaa);  // invalid
  CHECK(!watch.settled());
}

TEST(battery_pack_detected_at_boot) {
  const uint16_t lipo[] = {3900, 3890, 3895};
  SampleScript charged{lipo, 3};
  CHECK(detect(charged, BatteryPack::TwoAaa) == BatteryPack::LiPo);
  CHECK_EQ(charged.reads, 3);  // stops as soon as three readings agree
  CHECK_EQ(charged.waits, 2);

  const uint16_t nimh[] = {2480};
  SampleScript swapped{nimh, 1};
  CHECK(detect(swapped, BatteryPack::LiPo) == BatteryPack::LiPo);  // never proven: chosen by hand
  CHECK_EQ(swapped.reads, 0);  // with LiPo selected nothing is sampled: no boot delay

  const uint16_t fresh_alkaline[] = {3350};
  SampleScript ambiguous{fresh_alkaline, 1};
  CHECK(detect(ambiguous, BatteryPack::TwoAaa) == BatteryPack::TwoAaa);

  SampleScript on_usb{lipo, 3};
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
