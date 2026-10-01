#include "crash_record.hpp"

#include <cstring>

#include "crc32.hpp"

namespace badge::diag {

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

namespace {
uint32_t rec_crc(const CrashRecord &r) { return crc32(&r, offsetof(CrashRecord, crc)); }

// Reasons that can legitimately be left in the record before a watchdog
// reset. Anything else (even with a valid CRC) is not trusted.
bool pending_is_watchdog_reason(uint32_t p) {
  if (p >= uint32_t(ResetKind::Unknown)) return false;  // range first: the enum is 8 bits wide
  switch (ResetKind(p)) {
    case ResetKind::WatchdogHang:
    case ResetKind::Panic:
    case ResetKind::HardFault:
    case ResetKind::SoftReboot:
    case ResetKind::SleepWake:
      return true;
    default:
      return false;
  }
}
}  // namespace

bool record_valid(const CrashRecord &r) { return r.magic == kRecordMagic && r.crc == rec_crc(r); }

void record_seal(CrashRecord &r) { r.crc = rec_crc(r); }

void record_note(CrashRecord &r, ResetKind why, uint32_t pc, const char *msg) {
  r.pending = uint32_t(why);
  r.fault_pc = pc;
  std::strncpy(r.message, msg ? msg : "", sizeof r.message - 1);
  r.message[sizeof r.message - 1] = 0;
  record_seal(r);
}

BootInfo classify_boot(CrashRecord &rec, const ResetFlags &f, bool force_safe) {
  const bool valid = record_valid(rec);
  ResetKind kind;
  if (f.watchdog) {
    // RAM was retained. Without a valid record the boot ROM or a debugger
    // overwrote it (e.g. after BOOTSEL / UF2 flashing): cause unknown.
    if (!valid) kind = ResetKind::Unknown;
    else if (pending_is_watchdog_reason(rec.pending)) kind = ResetKind(rec.pending);
    else kind = ResetKind::WatchdogHang;  // nobody announced it: something stopped feeding
  } else if (f.run_pin) {
    kind = ResetKind::ResetPin;
  } else if (f.debugger) {
    kind = ResetKind::Debugger;
  } else {
    kind = ResetKind::PowerOn;  // POR / brown-out (or no flag at all)
  }

  // A cold start (power loss or unusable record) starts fresh counters. A
  // still-valid record after a POR is stale SRAM content and is discarded.
  const bool fresh = !valid || kind == ResetKind::PowerOn;
  const CrashRecord old = rec;
  if (fresh) {
    std::memset(&rec, 0, sizeof rec);
    rec.magic = kRecordMagic;
  }
  const bool abnormal = kind == ResetKind::WatchdogHang || kind == ResetKind::Panic || kind == ResetKind::HardFault;
  if (abnormal) ++rec.crash_streak;
  else if (kind != ResetKind::ResetPin && kind != ResetKind::Unknown) rec.crash_streak = 0;
  ++rec.boot_count;

  BootInfo bi{};
  bi.kind = kind;
  bi.boot_count = rec.boot_count;
  bi.crash_streak = rec.crash_streak;
  bi.safe_mode = force_safe || rec.crash_streak >= kSafeModeStreak;
  bi.record_was_valid = valid;
  if (abnormal && valid) {
    bi.fault_pc = old.fault_pc;
    std::memcpy(bi.message, old.message, sizeof bi.message);
    bi.message[sizeof bi.message - 1] = 0;
  }

  rec.pending = uint32_t(ResetKind::Unknown);
  rec.fault_pc = 0;
  rec.message[0] = 0;
  record_seal(rec);
  return bi;
}

}  // namespace badge::diag
