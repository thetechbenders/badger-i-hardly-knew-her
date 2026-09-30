// Qwiic / I2C0 on the Badger 2040 (SDA GPIO4, SCL GPIO5 per the SDK board
// header). Four-wire connector: 3V3, GND, SDA, SCL - no interrupt line.
#pragma once

#include "i2c_bus.hpp"

namespace badge {

class Rp2040I2c : public I2cBus {
 public:
  void init(uint32_t baud = 400000);
  bool write(uint8_t addr, const uint8_t *data, size_t len) override;
  bool write_read(uint8_t addr, uint8_t reg, uint8_t *out, size_t len) override;
  void recover() override;
  uint32_t recoveries() const { return recoveries_; }

 private:
  uint32_t baud_ = 400000;
  uint32_t recoveries_ = 0;
};

}  // namespace badge
