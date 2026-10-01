// APDS-9960 gesture sensor driver + service over Qwiic/I2C (address 0x39).
//
// Polled: the Qwiic cable carries no interrupt line, so the gesture FIFO is
// read over I2C while gesture mode is on. The service converges on the
// wanted state and never blocks the caller for more than a few bounded I2C
// transactions per poll.
//
//   Unprobed --probe--> Absent (no ACK / wrong ID; retried with backoff while wanted)
//                    \-> Standby (found, ENABLE = 0: engine and IR LED off)
//   Standby  --wanted--> Active (PON|WEN|PEN|GEN; IR LED pulses)
//   Active   --not wanted / sleep--> Standby
//   any      --3 consecutive I2C errors--> Fault (best-effort power-down,
//            bus recovery, re-probe with backoff 1 s .. 30 s while wanted)
//
// The backoff only resets after kStableMs without a fault, not on the next
// successful probe: a flaky cable that probes fine but fails once active
// would otherwise cycle every second, and every ok/fault flip of the status
// indicator costs an e-paper refresh.
//
// A missing sensor or bus fault only affects gestures; callers keep working.
#pragma once

#include <cstdint>

#include "gesture.hpp"
#include "i2c_bus.hpp"

namespace badge {

enum class SensorState : uint8_t { Unprobed, Absent, Standby, Active, Fault };
const char *sensor_state_str(SensorState s);

struct SensorStats {
  uint8_t chip_id = 0;
  uint32_t i2c_errors = 0, probes = 0, faults = 0;
  uint32_t sessions = 0, recognized = 0, rejected = 0, overflows = 0;
  SessionResult last{};
  Swipe last_swipe = Swipe::None;
};

class Apds9960 {
 public:
  static constexpr uint8_t kAddr = 0x39;
  static constexpr uint32_t kPollMs = 10;
  static constexpr uint32_t kRetryMinMs = 1000;
  static constexpr uint32_t kRetryMaxMs = 30000;
  static constexpr uint32_t kStableMs = 60000;

  explicit Apds9960(I2cBus &bus) : bus_(bus) {}
  void set_params(const GestureParams &p);
  // Probe once and force the sensor into its powered-down state (it keeps its
  // registers across a soft reset of the RP2040 on USB power).
  void start(uint32_t now_ms);
  void set_wanted(bool on);
  // Returns an accepted swipe in badge orientation (after gate), or None.
  Swipe poll(uint32_t now_ms);
  // Power the sensor down now (before sleep); ignores errors.
  void shutdown();

  SensorState state() const { return state_; }
  bool wanted() const { return wanted_; }
  const SensorStats &stats() const { return stats_; }
  uint32_t gate_suppressed() const { return gate_.suppressed(); }

 private:
  bool reg_write(uint8_t reg, uint8_t v);
  bool reg_read(uint8_t reg, uint8_t *v);
  bool probe_and_configure();
  bool power(bool on);
  void io_error(uint32_t now_ms);
  void schedule_retry(uint32_t now_ms);
  void became_ready(uint32_t now_ms);

  I2cBus &bus_;
  GestureParams params_;
  GestureDecoder decoder_;
  SwipeGate gate_;
  SensorState state_ = SensorState::Unprobed;
  bool wanted_ = false;
  uint8_t consec_errors_ = 0;
  uint32_t next_poll_ms_ = 0, next_retry_ms_ = 0, backoff_ms_ = kRetryMinMs;
  uint32_t ready_since_ms_ = 0;  // entered Standby/Active without a fault since
  SensorStats stats_;
};

}  // namespace badge
