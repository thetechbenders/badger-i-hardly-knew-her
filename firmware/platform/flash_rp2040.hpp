// Settings flash backend for the RP2040.
//
// XIP constraints: while a sector is erased or programmed the flash cannot
// be read, so no code may execute from flash on either core and no IRQ
// handler in flash may run. With core 1 running we use flash_safe_execute(),
// which parks core 1 in a RAM-resident lockout handler (core 1 registers via
// flash_safe_execute_core_init()) and disables IRQs on core 0. In
// single-core mode core 1 is never launched (it sits in the boot ROM), so
// disabling IRQs on core 0 is sufficient. The SDK's flash_range_erase /
// flash_range_program run from RAM and flush the XIP cache afterwards.
#pragma once

#include "settings_store.hpp"

namespace badge {

class Rp2040Flash : public FlashBackend {
 public:
  explicit Rp2040Flash(bool core1_running) : core1_running_(core1_running) {}
  size_t sector_size() const override;
  int sector_count() const override;
  bool read(uint32_t offset, void *dst, size_t len) override;
  bool erase_and_program(uint32_t sector_offset, const void *src, size_t len) override;
  void set_core1_running(bool r) { core1_running_ = r; }
  int last_error() const { return last_error_; }
  uint32_t last_duration_us() const { return last_us_; }

 private:
  bool core1_running_;
  int last_error_ = 0;
  uint32_t last_us_ = 0;
};

}  // namespace badge
