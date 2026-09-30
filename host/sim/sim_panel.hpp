// Simulated UC8151 panel for host tests: models BUSY duration per speed,
// records every command, and flags any call made while BUSY.
#pragma once

#include <atomic>
#include <vector>

#include "display_pipeline.hpp"

namespace badge {

class SimPanel : public Panel {
 public:
  struct Op {
    char kind;  // 'I' init, 'S' speed, 'F' full, 'P' partial, 'O' off
    uint8_t speed;
    Rect r;
  };
  explicit SimPanel(const std::atomic<uint32_t> *clock) : clock_(clock) {}
  void init(uint8_t speed) override;
  void set_speed(uint8_t speed) override;
  uint8_t speed() const override { return speed_; }
  bool busy() override;
  void start_full(const Framebuffer &fb) override;
  void start_partial(const Framebuffer &fb, Rect r) override;
  void finish() override;
  uint32_t expected_ms() const override;

  std::vector<Op> ops;
  Framebuffer image;          // what the panel would show
  int violations = 0;         // commands issued while busy
  bool stuck = false;         // simulate a BUSY line that never clears

 private:
  uint32_t now() const { return clock_->load(); }
  const std::atomic<uint32_t> *clock_;
  uint8_t speed_ = 0;
  uint32_t busy_until_ = 0;
};

}  // namespace badge
