#include <atomic>
#include <thread>

#include "check.hpp"
#include "spsc_queue.hpp"

using namespace badge;

TEST(spsc_bounded_fifo) {
  SpscQueue<int, 4> q;
  int v;
  CHECK(!q.pop(&v));
  for (int i = 0; i < 4; ++i) CHECK(q.push(i));
  CHECK(!q.push(99));  // full: refused, never overwritten
  CHECK_EQ(q.drops(), 1u);
  CHECK_EQ(q.size(), 4u);
  for (int i = 0; i < 4; ++i) { CHECK(q.pop(&v)); CHECK_EQ(v, i); }
  CHECK(q.empty());
  // Index wrap-around.
  for (int round = 0; round < 1000; ++round) { CHECK(q.push(round)); CHECK(q.pop(&v)); CHECK_EQ(v, round); }
}

TEST(spsc_two_thread_stress_preserves_order) {
  static SpscQueue<uint32_t, 8> q;
  constexpr uint32_t kN = 400000;
  std::atomic<bool> bad{false};
  std::thread consumer([&] {
    uint32_t expect = 0, v;
    while (expect < kN) {
      if (q.pop(&v)) {
        if (v != expect) bad = true;
        ++expect;
      }
    }
  });
  for (uint32_t i = 0; i < kN;) {
    if (q.push(i)) ++i;
  }
  consumer.join();
  CHECK(!bad.load());
  CHECK(q.empty());
}
