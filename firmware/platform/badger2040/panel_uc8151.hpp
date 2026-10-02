// Panel implementation over Pimoroni's UC8151_Legacy driver (the driver the
// Pimoroni C++ Badger2040 library uses for this panel).
//
// Thread-safety: the driver keeps no locks, busy-waits in reset()/setup(),
// and drives spi0 plus the CS/DC/RESET/BUSY GPIOs directly. It is therefore
// only ever called from the display context (core 1, or core 0 in
// single-core diagnostic mode) through this class.
//
// Bounded waits: the driver's busy_wait() has no timeout, so a controller
// that holds BUSY low would hang the display context inside reset(). Before
// every driver call that resets the controller we pulse RESET ourselves and
// wait for BUSY with a timeout; only a controller that answers is handed to
// the driver. (A controller that dies later, during the LUT upload right
// after a good reset, can still hang it; the watchdog covers that case.)
#pragma once

#include "display_pipeline.hpp"
#include "uc8151_pack.hpp"

namespace badge {

class Uc8151Panel : public Panel {
 public:
  static constexpr uint32_t kResetTimeoutMs = 500;
  bool init(uint8_t speed) override;
  bool set_speed(uint8_t speed) override;
  uint8_t speed() const override { return speed_; }
  bool busy() override;
  void start_full(const Framebuffer &fb) override;
  void start_partial(const Framebuffer &fb, Rect r) override;
  bool supports_partial() const override { return true; }
  Rect partial_window(Rect diff) const override { return uc8151::partial_window(diff); }
  void finish() override;
  uint32_t expected_ms() const override;

 private:
  bool reset_responds();
  uint8_t speed_ = 0;
};

}  // namespace badge
