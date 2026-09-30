// Reset classification, crash records that survive watchdog reboots,
// fault/panic hooks, assertions, watchdog and memory reporting.
#pragma once

#include <cstddef>
#include <cstdint>

#include "crash_record.hpp"  // ResetKind, BootInfo, classification rules

namespace badge::diag {

constexpr uint32_t kWatchdogMs = 5000;
constexpr uint32_t kHealthyUptimeMs = 60000;  // clears the crash streak

// Called first thing in main(): classifies the reset and decides safe mode.
const BootInfo &boot(bool force_safe);
const BootInfo &info();
void start_watchdog();
void feed_watchdog();
void mark_healthy();  // after kHealthyUptimeMs without faults
// A watchdog reset is expected soon (the caller stopped feeding it on
// purpose): leave `msg` for the next boot. clear_pending() withdraws it if
// the condition goes away before the watchdog fires.
void note_hang(const char *msg);
void clear_pending();
// Intentional reboots record their reason first.
[[noreturn]] void reboot(ResetKind why, bool to_bootsel = false);

void paint_stacks();  // call early on each core before its stack is deep
struct MemReport {
  uint32_t flash_used, ram_static, heap_free_est;
  uint32_t stack0_size, stack0_min_free, stack1_size, stack1_min_free;
};
MemReport memory();

}  // namespace badge::diag

// Assertion that survives release builds; records file:line then reboots
// through the watchdog so the next boot can report it.
#define BADGE_ASSERT(cond)                                        \
  do {                                                            \
    if (!(cond)) ::badge_assert_fail(__FILE__, __LINE__, #cond);  \
  } while (0)
extern "C" [[noreturn]] void badge_assert_fail(const char *file, int line, const char *expr);
