#include "panel_uc8151.hpp"

#include "drivers/uc8151_legacy/uc8151_legacy.hpp"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include "uc8151_pack.hpp"

namespace badge {

namespace {
// Driver-owned transfer buffer in the controller's layout (uc8151_pack.hpp).
uint8_t g_panel_buffer[uc8151::kBytes];
pimoroni::UC8151_Legacy g_uc(Framebuffer::kWidth, Framebuffer::kHeight, g_panel_buffer, spi0,
                             BADGER2040_INKY_CSN_PIN, BADGER2040_INKY_DC_PIN, BADGER2040_INKY_SCK_PIN,
                             BADGER2040_INKY_MOSI_PIN, BADGER2040_INKY_BUSY_PIN, BADGER2040_INKY_RESET_PIN);
}  // namespace

// Same pin setup and reset pulse as UC8151_Legacy::init()/reset(), but the
// wait for BUSY (low = busy) is bounded.
bool Uc8151Panel::reset_responds() {
  gpio_init(BADGER2040_INKY_RESET_PIN);
  gpio_put(BADGER2040_INKY_RESET_PIN, 1);
  gpio_set_dir(BADGER2040_INKY_RESET_PIN, GPIO_OUT);
  gpio_init(BADGER2040_INKY_BUSY_PIN);
  gpio_set_dir(BADGER2040_INKY_BUSY_PIN, GPIO_IN);
  gpio_set_pulls(BADGER2040_INKY_BUSY_PIN, true, false);
  gpio_put(BADGER2040_INKY_RESET_PIN, 0);
  sleep_ms(10);
  gpio_put(BADGER2040_INKY_RESET_PIN, 1);
  sleep_ms(10);
  const absolute_time_t deadline = make_timeout_time_ms(kResetTimeoutMs);
  while (!gpio_get(BADGER2040_INKY_BUSY_PIN)) {
    if (time_reached(deadline)) return false;
    sleep_us(200);
  }
  return true;
}

bool Uc8151Panel::init(uint8_t speed) {
  speed_ = 0;
  if (!reset_responds()) return false;
  g_uc.init();  // SPI + pins, reset, setup(0)
  return speed ? set_speed(speed) : true;
}

bool Uc8151Panel::set_speed(uint8_t speed) {
  if (!reset_responds()) return false;
  g_uc.update_speed(speed);  // reset + LUT upload; blocks for a few ms
  speed_ = speed;
  return true;
}

bool Uc8151Panel::busy() { return g_uc.is_busy(); }

void Uc8151Panel::start_full(const Framebuffer &fb) {
  uc8151::pack(fb, g_panel_buffer);
  g_uc.update(false);  // non-blocking: returns after DRF, BUSY stays low while refreshing
}

void Uc8151Panel::start_partial(const Framebuffer &fb, Rect r) {
  uc8151::pack(fb, g_panel_buffer);
  // Driver contract: y/h multiples of 8 (column "banks"); x/w in pixels.
  g_uc.partial_update(r.x, r.y, r.w, r.h, false);
}

void Uc8151Panel::finish() { g_uc.power_off(); }

uint32_t Uc8151Panel::expected_ms() const { return g_uc.update_time(); }

}  // namespace badge
