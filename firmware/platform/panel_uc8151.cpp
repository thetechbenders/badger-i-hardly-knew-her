#include "panel_uc8151.hpp"

#include <cstring>

#include "drivers/uc8151_legacy/uc8151_legacy.hpp"

namespace badge {

namespace {
// Driver-owned transfer buffer; same layout as badge::Framebuffer.
uint8_t g_panel_buffer[Framebuffer::kBytes];
pimoroni::UC8151_Legacy g_uc(Framebuffer::kWidth, Framebuffer::kHeight, g_panel_buffer, spi0,
                             BADGER2040_INKY_CSN_PIN, BADGER2040_INKY_DC_PIN, BADGER2040_INKY_SCK_PIN,
                             BADGER2040_INKY_MOSI_PIN, BADGER2040_INKY_BUSY_PIN, BADGER2040_INKY_RESET_PIN);
}  // namespace

void Uc8151Panel::init(uint8_t speed) {
  g_uc.init();  // SPI + pins, reset, setup(0)
  speed_ = 0;
  if (speed) set_speed(speed);
}

void Uc8151Panel::set_speed(uint8_t speed) {
  g_uc.update_speed(speed);  // reset + LUT upload; blocks for a few ms
  speed_ = speed;
}

bool Uc8151Panel::busy() { return g_uc.is_busy(); }

void Uc8151Panel::start_full(const Framebuffer &fb) {
  std::memcpy(g_panel_buffer, fb.data(), Framebuffer::kBytes);
  g_uc.update(false);  // non-blocking: returns after DRF, BUSY stays low while refreshing
}

void Uc8151Panel::start_partial(const Framebuffer &fb, Rect r) {
  std::memcpy(g_panel_buffer, fb.data(), Framebuffer::kBytes);
  // Driver contract: y/h multiples of 8 (column "banks"); x/w in pixels.
  g_uc.partial_update(r.x, r.y, r.w, r.h, false);
}

void Uc8151Panel::finish() { g_uc.power_off(); }

uint32_t Uc8151Panel::expected_ms() const { return g_uc.update_time(); }

}  // namespace badge
