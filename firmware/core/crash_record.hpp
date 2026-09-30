// Reset classification and the crash record kept in uninitialised RAM.
//
// Pure logic (no SDK headers) so it is tested on the host; the platform
// layer (diagnostics.cpp) reads the reset registers and owns the record.
//
// The record survives watchdog and soft resets but is garbage after power
// loss, and SRAM can also retain an old, still-valid record across a short
// power dip. Classification therefore trusts the hardware first:
//   - WATCHDOG.REASON is cleared by every chip-level reset (POR, brown-out,
//     RUN pin, debugger PSM restart) and set only by a watchdog reset, so it
//     alone decides "watchdog reset" vs "chip reset".
//   - CHIP_RESET.HAD_* is only consulted for chip resets. Its bits describe
//     the last chip-level reset and are not updated by a watchdog reset, so
//     they must never be used to classify one.
//   - The record (magic + CRC) is used only for watchdog resets: it carries
//     the reason an intentional reset or a fault handler left behind.
#pragma once

#include <cstddef>
#include <cstdint>

namespace badge::diag {

enum class ResetKind : uint8_t {
  PowerOn = 0,   // POR / brown-out / battery wake (RAM contents lost)
  ResetPin,      // RESET button (RUN pin)
  Debugger,      // PSM restart via SWD
  WatchdogHang,  // watchdog expired: something stopped feeding it
  Panic,         // SDK panic() / failed assert()
  HardFault,
  SoftReboot,    // `reboot` command
  SleepWake,     // emulated sleep on USB power, woken by a button
  Unknown,
};
const char *reset_kind_str(ResetKind k);

constexpr uint32_t kRecordMagic = 0xBAD6E001;
constexpr uint32_t kSafeModeStreak = 3;

struct CrashRecord {
  uint32_t magic;
  uint32_t boot_count;
  uint32_t crash_streak;
  uint32_t pending;  // ResetKind recorded right before an intentional or fault reset
  uint32_t fault_pc;
  char message[64];
  uint32_t crc;
};

bool record_valid(const CrashRecord &r);
void record_seal(CrashRecord &r);
// Store the reason for the reset that is about to happen (then seal).
void record_note(CrashRecord &r, ResetKind why, uint32_t pc, const char *msg);

struct ResetFlags {
  bool watchdog;   // WATCHDOG.REASON != 0
  bool por;        // CHIP_RESET.HAD_POR
  bool run_pin;    // CHIP_RESET.HAD_RUN
  bool debugger;   // CHIP_RESET.HAD_PSM_RESTART
};

struct BootInfo {
  ResetKind kind;
  uint32_t boot_count;    // since last power-on
  uint32_t crash_streak;  // consecutive abnormal resets
  bool safe_mode;         // crash_streak >= kSafeModeStreak or forced
  bool record_was_valid;  // false: cold RAM (or clobbered, e.g. by the boot ROM)
  char message[64];       // panic / fault detail from the previous run
  uint32_t fault_pc;
};

// Classify this boot from the reset flags and the record, then rewrite the
// record for the new run (counters updated, pending reason cleared, sealed).
BootInfo classify_boot(CrashRecord &rec, const ResetFlags &f, bool force_safe);

}  // namespace badge::diag
