// Bounded single-producer / single-consumer ring buffer.
//
// Ownership rules (enforced by design, checked by tests):
//   - Exactly one execution context calls push() and exactly one calls pop().
//     Producer and consumer may be different cores or an ISR and a thread.
//   - Only 32-bit atomic loads/stores with acquire/release ordering are used,
//     which are lock-free on the Cortex-M0+ (plain LDR/STR plus DMB); no
//     read-modify-write atomics, so no libatomic or spinlock is needed.
//   - Capacity N must be a power of two; the queue holds up to N items.
//   - push() never blocks: a full queue returns false and the caller decides
//     (drop, coalesce, or retry later). drops() counts refused pushes.
#pragma once

#include <atomic>
#include <cstdint>

namespace badge {

template <typename T, uint32_t N>
class SpscQueue {
  static_assert(N >= 2 && (N & (N - 1)) == 0, "capacity must be a power of two");
  static_assert(sizeof(std::atomic<uint32_t>) == 4, "expect word-sized atomics");

 public:
  bool push(const T &v) {
    const uint32_t h = head_.load(std::memory_order_relaxed);
    const uint32_t t = tail_.load(std::memory_order_acquire);
    if (h - t >= N) {
      drops_.store(drops_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
      return false;
    }
    buf_[h & (N - 1)] = v;
    head_.store(h + 1, std::memory_order_release);
    return true;
  }

  bool pop(T *out) {
    const uint32_t t = tail_.load(std::memory_order_relaxed);
    const uint32_t h = head_.load(std::memory_order_acquire);
    if (h == t) return false;
    *out = buf_[t & (N - 1)];
    tail_.store(t + 1, std::memory_order_release);
    return true;
  }

  uint32_t size() const {
    return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire);
  }
  bool empty() const { return size() == 0; }
  static constexpr uint32_t capacity() { return N; }
  // Only meaningful to the producer (single writer).
  uint32_t drops() const { return drops_.load(std::memory_order_relaxed); }

 private:
  T buf_[N];
  std::atomic<uint32_t> head_{0};
  std::atomic<uint32_t> tail_{0};
  std::atomic<uint32_t> drops_{0};
};

}  // namespace badge
