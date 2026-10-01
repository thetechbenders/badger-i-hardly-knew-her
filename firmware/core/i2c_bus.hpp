// Minimal I2C bus interface (platform: Pico SDK i2c with timeouts; host: fakes).
#pragma once

#include <cstddef>
#include <cstdint>

namespace badge {

class I2cBus {
 public:
  virtual ~I2cBus() = default;
  // Every call must be bounded in time and return false on NACK/timeout.
  virtual bool write(uint8_t addr, const uint8_t *data, size_t len) = 0;
  virtual bool write_read(uint8_t addr, uint8_t reg, uint8_t *out, size_t len) = 0;
  // Attempt to free a stuck bus (clock out a held SDA, issue STOP).
  virtual void recover() {}
};

}  // namespace badge
