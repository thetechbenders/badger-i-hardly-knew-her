// Badger 2350 CYW43 passive survey adapter. Core-0 polling only.
#pragma once
#include <cstddef>
#include <cstdint>
#include "pico/cyw43_arch.h"
#include "rf_cryptid.hpp"
namespace badge {
class RfWifiScanner {
 public:
  void request(uint32_t now_ms);
  bool poll(uint32_t now_ms, bool visible);
  const RfPlate &plate() const { return plate_; }
 private:
  static int on_result(void *env, const cyw43_ev_scan_result_t *result);
  void receive(const cyw43_ev_scan_result_t &result);
  void stop();
  RfObservation rows_[kRfMaxAps] = {};
  size_t count_ = 0;
  uint32_t started_ = 0;
  bool initialized_ = false, scanning_ = false;
  RfPlate plate_;
};
}
