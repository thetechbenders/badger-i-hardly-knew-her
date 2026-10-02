// SSD1680 e-paper panel of the Badger 2350 (264 x 176), native driver.
//
// The command sequence, RAM window and waveform (LUT and voltages) follow
// Pimoroni's reference driver (pimoroni/badger2350 v3.1.1,
// modules/c/ssd1680/ssd1680.cpp, MIT; third_party/licenses/), the code the
// badge ships with. It is reimplemented here with this firmware's display
// service rules:
//   - only called from the display context (core 1, or core 0 in
//     single-core diagnostic mode), through DisplayService;
//   - no unbounded waits: BUSY (active HIGH on this controller) is polled
//     with a timeout everywhere the reference spins; a controller that
//     never answers is a panel fault, never a hang of the display context;
//   - start_full() returns once the update is triggered; DisplayService
//     polls busy() and times the refresh out. If a BUSY wait before the
//     trigger times out it sends nothing further and returns false, and
//     DisplayService treats that as a panel fault.
//
// Refresh modes: the reference has one waveform and no partial update, and
// its speed setting is a no-op. This backend therefore offers full refreshes
// only (supports_partial() is false; the refresh policy reports "no partial
// on this panel"), and set_speed() accepts any value without changing the
// waveform. A clean refresh is the same full refresh. Four-tone encoding:
// see ssd1680_pack.hpp.
#pragma once

#include "display_pipeline.hpp"

namespace badge {

class Ssd1680Panel : public Panel {
 public:
  static constexpr uint32_t kResetTimeoutMs = 500;    // reset / software reset / LUT load
  static constexpr uint32_t kCommandTimeoutMs = 100;  // update sequence before the trigger
  static constexpr uint32_t kSpiHz = 12'000'000;      // as the reference driver

  bool init(uint8_t speed) override;
  bool set_speed(uint8_t speed) override;
  uint8_t speed() const override { return speed_; }
  bool busy() override;
  bool start_full(const Framebuffer &fb) override;
  void start_partial(const Framebuffer &fb, Rect r) override;  // never called
  bool supports_partial() const override { return false; }
  void finish() override;
  // Not measured on this panel yet; nothing depends on it (the display
  // service times refreshes by BUSY, with DisplayService::kBusyTimeoutMs).
  uint32_t expected_ms() const override { return 0; }

 private:
  bool reset_and_setup();
  bool write_waveform();
  bool wait_idle(uint32_t timeout_ms);
  uint8_t speed_ = 0;
};

}  // namespace badge
