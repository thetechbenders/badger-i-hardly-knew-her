// Panel implementation over Pimoroni's UC8151_Legacy driver (the driver the
// Pimoroni C++ Badger2040 library uses for this panel).
//
// Thread-safety: the driver keeps no locks, busy-waits in reset()/setup(),
// and drives spi0 plus the CS/DC/RESET/BUSY GPIOs directly. It is therefore
// only ever called from the display context (core 1, or core 0 in
// single-core diagnostic mode) through this class.
#pragma once

#include "display_pipeline.hpp"

namespace badge {

class Uc8151Panel : public Panel {
 public:
  void init(uint8_t speed) override;
  void set_speed(uint8_t speed) override;
  uint8_t speed() const override { return speed_; }
  bool busy() override;
  void start_full(const Framebuffer &fb) override;
  void start_partial(const Framebuffer &fb, Rect r) override;
  void finish() override;
  uint32_t expected_ms() const override;

 private:
  uint8_t speed_ = 0;
};

}  // namespace badge
