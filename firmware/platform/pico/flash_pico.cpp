#include "flash_pico.hpp"

#include <cstring>

#include "flash_layout.hpp"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/flash.h"
#include "pico/stdlib.h"

namespace badge {

namespace {
struct Op {
  uint32_t flash_offset;
  const uint8_t *src;
  size_t len;
  size_t erase_len;
};
// Programming works in 256-byte pages; stage the padded copy in RAM because
// the source must not live in flash while XIP is off.
uint8_t g_page_buf[2 * flash_layout::kSectorSize];  // one 8 KiB slot

void do_erase_program(void *p) {
  const Op *op = static_cast<const Op *>(p);
  flash_range_erase(op->flash_offset, op->erase_len);
  if (op->len) {
    const size_t n = (op->len + FLASH_PAGE_SIZE - 1) & ~size_t(FLASH_PAGE_SIZE - 1);
    flash_range_program(op->flash_offset, g_page_buf, n);
  }
}
}  // namespace

size_t PicoFlash::sector_size() const { return flash_layout::kSectorSize; }
size_t PicoFlash::region_size() const { return flash_layout::kSettingsSectors * flash_layout::kSectorSize; }

bool PicoFlash::read(uint32_t offset, void *dst, size_t len) {
  if (offset + len > flash_layout::kSettingsSectors * flash_layout::kSectorSize) return false;
  std::memcpy(dst, reinterpret_cast<const void *>(XIP_BASE + flash_layout::kSettingsOffset + offset), len);
  return true;
}

bool PicoFlash::erase_and_program(uint32_t sector_offset, const void *src, size_t len, size_t erase_len) {
  if (sector_offset % flash_layout::kSectorSize || erase_len % flash_layout::kSectorSize || erase_len == 0 ||
      erase_len > sizeof g_page_buf || len > erase_len ||
      sector_offset + erase_len > flash_layout::kSettingsSectors * flash_layout::kSectorSize) {
    last_error_ = -100;
    return false;
  }
  std::memset(g_page_buf, 0xFF, sizeof g_page_buf);
  if (len) std::memcpy(g_page_buf, src, len);
  Op op{flash_layout::kSettingsOffset + sector_offset, g_page_buf, len, erase_len};
  const uint32_t t0 = time_us_32();
  int rc;
  if (core1_running_) {
    rc = flash_safe_execute(do_erase_program, &op, 1000);
  } else {
    const uint32_t irq = save_and_disable_interrupts();
    do_erase_program(&op);
    restore_interrupts(irq);
    rc = PICO_OK;
  }
  last_us_ = time_us_32() - t0;
  last_error_ = rc;
  return rc == PICO_OK;
}

}  // namespace badge
