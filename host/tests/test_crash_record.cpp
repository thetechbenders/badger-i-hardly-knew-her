#include <cstring>
#include <initializer_list>

#include "check.hpp"
#include "crash_record.hpp"

using namespace badge::diag;

namespace {
// Chip-reset flags as the RP2040 reports them. CHIP_RESET keeps describing the
// last chip-level reset, so after a watchdog reset that followed a power-on it
// still reads HAD_POR; only WATCHDOG.REASON tells the two apart.
constexpr ResetFlags kPowerOn{false, true, false, false};
constexpr ResetFlags kWatchdogAfterPowerOn{true, true, false, false};
constexpr ResetFlags kResetPin{false, false, true, false};

CrashRecord garbage() {
  CrashRecord r;
  std::memset(&r, 0x5A, sizeof r);
  return r;
}
}  // namespace

TEST(crash_record_cold_boot_rejects_garbage_and_starts_fresh) {
  CrashRecord r = garbage();
  const BootInfo b = classify_boot(r, kPowerOn, false);
  CHECK(b.kind == ResetKind::PowerOn);
  CHECK(!b.record_was_valid);
  CHECK_EQ(b.boot_count, 1u);
  CHECK_EQ(b.crash_streak, 0u);
  CHECK(!b.safe_mode);
  CHECK_EQ(b.message[0], 0);
  CHECK(record_valid(r));  // resealed for this run
}

// SRAM can keep a sealed record across a short power dip: a POR must still
// discard it (no stale streak, no stale message).
TEST(crash_record_valid_record_after_power_on_is_discarded) {
  CrashRecord r = garbage();
  classify_boot(r, kPowerOn, false);
  for (int i = 0; i < 2; ++i) {
    record_note(r, ResetKind::Panic, 0x10001234, "boom");
    classify_boot(r, kWatchdogAfterPowerOn, false);
  }
  record_note(r, ResetKind::HardFault, 0x10005678, "stale");
  const BootInfo b = classify_boot(r, kPowerOn, false);
  CHECK(b.kind == ResetKind::PowerOn);
  CHECK(b.record_was_valid);
  CHECK_EQ(b.boot_count, 1u);
  CHECK_EQ(b.crash_streak, 0u);
  CHECK_EQ(b.message[0], 0);
  CHECK_EQ(b.fault_pc, 0u);
}

// Regression: the old classifier checked CHIP_RESET.HAD_POR first, so every
// watchdog reset after a power-on looked like a fresh power-on and the crash
// streak (and safe mode) could never build up.
TEST(crash_record_watchdog_resets_after_power_on_build_a_streak) {
  CrashRecord r = garbage();
  classify_boot(r, kPowerOn, false);
  record_note(r, ResetKind::HardFault, 0x10000abc, "hard fault pc=10000abc lr=10000123 core1");
  BootInfo b = classify_boot(r, kWatchdogAfterPowerOn, false);
  CHECK(b.kind == ResetKind::HardFault);
  CHECK_EQ(b.boot_count, 2u);
  CHECK_EQ(b.crash_streak, 1u);
  CHECK_EQ(b.fault_pc, 0x10000abcu);
  CHECK_STR(b.message, "hard fault pc=10000abc lr=10000123 core1");
  // Unannounced watchdog expiry (core 0 hung) and an announced core 1 stall.
  b = classify_boot(r, kWatchdogAfterPowerOn, false);
  CHECK(b.kind == ResetKind::WatchdogHang);
  CHECK_EQ(b.message[0], 0);
  CHECK(!b.safe_mode);
  record_note(r, ResetKind::WatchdogHang, 0, "core1 display heartbeat stalled");
  b = classify_boot(r, kWatchdogAfterPowerOn, false);
  CHECK(b.kind == ResetKind::WatchdogHang);
  CHECK_STR(b.message, "core1 display heartbeat stalled");
  CHECK_EQ(b.crash_streak, 3u);
  CHECK(b.safe_mode);
}

TEST(crash_record_intentional_resets_clear_the_streak) {
  CrashRecord r = garbage();
  classify_boot(r, kPowerOn, false);
  record_note(r, ResetKind::Panic, 0, "assert x");
  classify_boot(r, kWatchdogAfterPowerOn, false);
  record_note(r, ResetKind::SoftReboot, 0, nullptr);
  BootInfo b = classify_boot(r, kWatchdogAfterPowerOn, false);
  CHECK(b.kind == ResetKind::SoftReboot);
  CHECK_EQ(b.crash_streak, 0u);
  CHECK_EQ(b.message[0], 0);
  // The RESET button neither counts as a crash nor clears a crash loop.
  record_note(r, ResetKind::Panic, 0, "again");
  classify_boot(r, kWatchdogAfterPowerOn, false);
  b = classify_boot(r, kResetPin, false);
  CHECK(b.kind == ResetKind::ResetPin);
  CHECK_EQ(b.crash_streak, 1u);
}

TEST(crash_record_integrity_failures_are_not_trusted) {
  CrashRecord r = garbage();
  classify_boot(r, kPowerOn, false);
  record_note(r, ResetKind::Panic, 0, "panic");
  r.message[3] ^= 1;  // bit flip after sealing
  BootInfo b = classify_boot(r, kWatchdogAfterPowerOn, false);
  CHECK(b.kind == ResetKind::Unknown);  // watchdog reset, but reason unreadable
  CHECK(!b.record_was_valid);
  CHECK_EQ(b.crash_streak, 0u);
  CHECK_EQ(b.message[0], 0);
  // A sealed record with a nonsense pending value (including one that would
  // truncate to a valid 8-bit enum) is a plain watchdog timeout.
  for (uint32_t bogus : {uint32_t(ResetKind::PowerOn), uint32_t(ResetKind::Debugger), 0x103u, 0xFFFFFFFFu}) {
    r.pending = bogus;
    record_seal(r);
    b = classify_boot(r, kWatchdogAfterPowerOn, false);
    CHECK(b.kind == ResetKind::WatchdogHang);
  }
  // An unterminated message is cut, never read past the buffer.
  record_note(r, ResetKind::Panic, 0, "x");
  std::memset(r.message, 'A', sizeof r.message);
  record_seal(r);
  b = classify_boot(r, kWatchdogAfterPowerOn, false);
  CHECK_EQ(std::strlen(b.message), sizeof b.message - 1);
}

TEST(crash_record_forced_safe_mode) {
  CrashRecord r = garbage();
  const BootInfo b = classify_boot(r, kPowerOn, true);
  CHECK(b.safe_mode);
  CHECK_EQ(b.crash_streak, 0u);
}
