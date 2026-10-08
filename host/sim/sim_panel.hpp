// Simulated panel for host tests: models BUSY duration per speed, records
// every command, and flags any call made while BUSY. By default it behaves
// like the UC8151 (partial windows in whole 8-row banks); with
// `partial = false` like a controller without partial refresh (the Badger
// 2350 SSD1680 backend).
#pragma once

#include <atomic>
#include <vector>

#include "display_pipeline.hpp"

namespace badge {

class SimPanel : public Panel {
 public:
  struct Op {
    char kind;  // 'I' init, 'S' speed, 'F' full, 'f' refused full, 'P' partial, 'O' off
    uint8_t speed;
    Rect r;
  };
  explicit SimPanel(const std::atomic<uint32_t> *clock) : clock_(clock) {}
  bool init(uint8_t speed) override;
  bool set_speed(uint8_t speed) override;
  uint8_t speed() const override { return speed_; }
  bool busy() override;
  bool start_full(const Framebuffer &fb) override;
  void start_partial(const Framebuffer &fb, Rect r) override;
  bool supports_partial() const override { return partial; }
  Rect partial_window(Rect diff) const override;
  void finish() override;
  uint32_t expected_ms() const override;

  std::vector<Op> ops;
  Framebuffer image;          // what the panel would show
  int violations = 0;         // commands issued while busy
  bool stuck = false;         // BUSY stuck low until the next reset (transient)
  bool dead = false;          // BUSY held low permanently: every reset times out
  bool partial = true;        // false: no partial refresh (start_partial is a violation)
  bool start_fails = false;   // start_full() is refused before the trigger (op 'f')

 private:
  uint32_t now() const { return clock_->load(); }
  const std::atomic<uint32_t> *clock_;
  uint8_t speed_ = 0;
  uint32_t busy_until_ = 0;
};

}  // namespace badge
