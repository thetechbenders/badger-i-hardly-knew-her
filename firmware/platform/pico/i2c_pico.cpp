#include "i2c_pico.hpp"

#include <initializer_list>

#include "board.hpp"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"

namespace badge {

namespace {
i2c_inst_t *const kI2c = board::kI2cInstance == 0 ? i2c0 : i2c1;
constexpr uint kSda = board::kI2cSdaPin, kScl = board::kI2cSclPin;
// Per-transaction timeout: generous for 400 kHz (a byte takes ~25 us) but
// small enough that a dead bus costs the main loop at most a few ms.
uint32_t timeout_us(size_t bytes) { return 1000 + uint32_t(bytes) * 100; }
}  // namespace

void PicoI2c::init(uint32_t baud) {
  baud_ = baud;
  i2c_init(kI2c, baud);
  gpio_set_function(kSda, GPIO_FUNC_I2C);
  gpio_set_function(kScl, GPIO_FUNC_I2C);
  // Weak internal pull-ups; Qwiic breakouts normally add their own.
  gpio_pull_up(kSda);
  gpio_pull_up(kScl);
}

bool PicoI2c::write(uint8_t addr, const uint8_t *data, size_t len) {
  return i2c_write_timeout_us(kI2c, addr, data, len, false, timeout_us(len)) == int(len);
}

bool PicoI2c::write_read(uint8_t addr, uint8_t reg, uint8_t *out, size_t len) {
  if (i2c_write_timeout_us(kI2c, addr, &reg, 1, true, timeout_us(1)) != 1) return false;
  return i2c_read_timeout_us(kI2c, addr, out, len, false, timeout_us(len)) == int(len);
}

void PicoI2c::recover() {
  // Standard bus clear with open-drain emulation (drive low / release to the
  // pull-up, never drive high): clock SCL up to 9 times until the stuck
  // device releases SDA, then generate a STOP and re-initialise.
  ++recoveries_;
  i2c_deinit(kI2c);
  auto low = [](uint pin) { gpio_put(pin, 0); gpio_set_dir(pin, GPIO_OUT); };
  auto release = [](uint pin) { gpio_set_dir(pin, GPIO_IN); };
  for (uint pin : {kSda, kScl}) {
    gpio_init(pin);
    gpio_pull_up(pin);
    release(pin);
  }
  for (int i = 0; i < 9 && !gpio_get(kSda); ++i) {
    low(kScl);
    sleep_us(5);
    release(kScl);
    sleep_us(5);
  }
  low(kSda);  // STOP: SDA rises while SCL is high
  sleep_us(5);
  release(kScl);
  sleep_us(5);
  release(kSda);
  sleep_us(5);
  init(baud_);
}

}  // namespace badge
