#include "diagnostics.hpp"

#include <malloc.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "crc32.hpp"
#include "hardware/structs/vreg_and_chip_reset.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "pico/platform.h"
#include "pico/stdlib.h"

namespace badge::diag {

namespace {
constexpr uint32_t kMagic = 0xBAD6E001;
constexpr uint32_t kPaint = 0xA5A5A5A5;

// Lives in .uninitialized_data: survives watchdog/soft resets, garbage after
// power loss (detected by magic + CRC).
struct Record {
  uint32_t magic;
  uint32_t boot_count;
  uint32_t crash_streak;
  uint32_t pending;  // ResetKind set right before an intentional reset
  uint32_t fault_pc;
  char message[64];
  uint32_t crc;
};
Record __uninitialized_ram(g_rec);
BootInfo g_info;

uint32_t rec_crc() { return crc32(&g_rec, offsetof(Record, crc)); }
void rec_seal() { g_rec.crc = rec_crc(); }
bool rec_valid() { return g_rec.magic == kMagic && g_rec.crc == rec_crc(); }

extern "C" {
extern char __StackLimit, __StackTop, __StackBottom, __StackOneBottom, __StackOneTop;
extern char __flash_binary_end, __bss_end__, end;
}
}  // namespace

const char *reset_kind_str(ResetKind k) {
  switch (k) {
    case ResetKind::PowerOn: return "power-on";
    case ResetKind::ResetPin: return "reset button";
    case ResetKind::Debugger: return "debugger";
    case ResetKind::WatchdogHang: return "watchdog timeout";
    case ResetKind::Panic: return "panic/assert";
    case ResetKind::HardFault: return "hard fault";
    case ResetKind::SoftReboot: return "reboot command";
    case ResetKind::SleepWake: return "wake from USB sleep";
    default: return "unknown";
  }
}

const BootInfo &boot(bool force_safe) {
  const uint32_t chip = vreg_and_chip_reset_hw->chip_reset;
  const bool valid = rec_valid();
  if (!valid) {
    std::memset(&g_rec, 0, sizeof g_rec);
    g_rec.magic = kMagic;
    g_rec.pending = uint32_t(ResetKind::Unknown);
  }
  ResetKind kind;
  if (!valid || (chip & VREG_AND_CHIP_RESET_CHIP_RESET_HAD_POR_BITS)) {
    kind = ResetKind::PowerOn;
    g_rec.crash_streak = 0;
    g_rec.boot_count = 0;
  } else if (chip & VREG_AND_CHIP_RESET_CHIP_RESET_HAD_RUN_BITS) {
    kind = ResetKind::ResetPin;
  } else if (chip & VREG_AND_CHIP_RESET_CHIP_RESET_HAD_PSM_RESTART_BITS) {
    kind = ResetKind::Debugger;
  } else if (g_rec.pending != uint32_t(ResetKind::Unknown)) {
    kind = ResetKind(g_rec.pending);
  } else if (watchdog_caused_reboot()) {
    kind = ResetKind::WatchdogHang;
  } else {
    kind = ResetKind::Unknown;
  }
  const bool abnormal = kind == ResetKind::WatchdogHang || kind == ResetKind::Panic ||
                        kind == ResetKind::HardFault;
  if (abnormal) ++g_rec.crash_streak;
  else if (kind != ResetKind::ResetPin && kind != ResetKind::Unknown) g_rec.crash_streak = 0;
  ++g_rec.boot_count;

  g_info.kind = kind;
  g_info.boot_count = g_rec.boot_count;
  g_info.crash_streak = g_rec.crash_streak;
  g_info.safe_mode = force_safe || g_rec.crash_streak >= kSafeModeStreak;
  g_info.fault_pc = abnormal ? g_rec.fault_pc : 0;
  std::memcpy(g_info.message, abnormal ? g_rec.message : "", abnormal ? sizeof g_info.message : 1);
  g_info.message[sizeof g_info.message - 1] = 0;

  g_rec.pending = uint32_t(ResetKind::Unknown);
  g_rec.fault_pc = 0;
  g_rec.message[0] = 0;
  rec_seal();
  return g_info;
}

const BootInfo &info() { return g_info; }

void start_watchdog() { watchdog_enable(kWatchdogMs, true); }
void feed_watchdog() { watchdog_update(); }

void mark_healthy() {
  if (g_rec.crash_streak) {
    g_rec.crash_streak = 0;
    rec_seal();
  }
}

static void record(ResetKind why, uint32_t pc, const char *msg) {
  g_rec.pending = uint32_t(why);
  g_rec.fault_pc = pc;
  std::strncpy(g_rec.message, msg ? msg : "", sizeof g_rec.message - 1);
  g_rec.message[sizeof g_rec.message - 1] = 0;
  rec_seal();
}

void reboot(ResetKind why, bool to_bootsel) {
  record(why, 0, nullptr);
  if (to_bootsel) {
    // A BOOTSEL reboot goes through the boot ROM; the record stays valid.
    reset_usb_boot(0, 0);
  }
  watchdog_reboot(0, 0, 10);
  while (true) tight_loop_contents();
}

void paint_stacks() {
  // Paint from just below the current stack pointer to the stack limit so
  // memory() can report the minimum free stack seen.
  uint32_t sp;
  asm volatile("mov %0, sp" : "=r"(sp));
  char *lo = get_core_num() == 0 ? &__StackBottom : &__StackOneBottom;
  for (uint32_t *p = reinterpret_cast<uint32_t *>(lo); reinterpret_cast<uint32_t>(p) + 64 < sp; ++p) *p = kPaint;
}

static uint32_t unpainted_from(char *bottom, char *top) {
  uint32_t n = 0;
  for (uint32_t *p = reinterpret_cast<uint32_t *>(bottom); p < reinterpret_cast<uint32_t *>(top) && *p == kPaint; ++p)
    n += 4;
  return n;
}

MemReport memory() {
  MemReport m{};
  m.flash_used = uint32_t(reinterpret_cast<uintptr_t>(&__flash_binary_end) - XIP_BASE);
  m.ram_static = uint32_t(reinterpret_cast<uintptr_t>(&__bss_end__) - SRAM_BASE);
  struct mallinfo mi = mallinfo();
  m.heap_free_est = uint32_t(reinterpret_cast<uintptr_t>(&__StackLimit) - reinterpret_cast<uintptr_t>(&end)) -
                    uint32_t(mi.arena) + uint32_t(mi.fordblks);
  m.stack0_size = uint32_t(&__StackTop - &__StackBottom);
  m.stack0_min_free = unpainted_from(&__StackBottom, &__StackTop);
  m.stack1_size = uint32_t(&__StackOneTop - &__StackOneBottom);
  m.stack1_min_free = unpainted_from(&__StackOneBottom, &__StackOneTop);
  return m;
}

}  // namespace badge::diag

using badge::diag::ResetKind;

extern "C" [[noreturn]] void badge_assert_fail(const char *file, int line, const char *expr) {
  char msg[64];
  const char *base = std::strrchr(file, '/');
  std::snprintf(msg, sizeof msg, "assert %s:%d %s", base ? base + 1 : file, line, expr);
  badge::diag::record(ResetKind::Panic, 0, msg);
  watchdog_reboot(0, 0, 10);
  while (true) tight_loop_contents();
}

// Installed via PICO_PANIC_FUNCTION=badge_panic: SDK panic() and failed
// assert() (newlib __assert_func -> panic) land here.
extern "C" [[noreturn]] void badge_panic(const char *fmt, ...) {
  char msg[64] = "panic";
  if (fmt) {
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
  }
  badge::diag::record(ResetKind::Panic, 0, msg);
  watchdog_reboot(0, 0, 10);
  while (true) tight_loop_contents();
}

extern "C" [[noreturn]] void badge_hardfault_c(const uint32_t *frame) {
  char msg[64];
  std::snprintf(msg, sizeof msg, "hard fault pc=%08lx lr=%08lx core%u", (unsigned long)frame[6],
                (unsigned long)frame[5], get_core_num());
  badge::diag::record(ResetKind::HardFault, frame[6], msg);
  watchdog_reboot(0, 0, 10);
  while (true) tight_loop_contents();
}

// Overrides the SDK's weak handler: pick MSP/PSP from EXC_RETURN, then C.
extern "C" __attribute__((naked)) void isr_hardfault() {
  asm volatile(
      "movs r0, #4\n"
      "mov r1, lr\n"
      "tst r0, r1\n"
      "beq 1f\n"
      "mrs r0, psp\n"
      "b 2f\n"
      "1: mrs r0, msp\n"
      "2: ldr r1, =badge_hardfault_c\n"
      "bx r1\n");
}
