#include <vector>

#include "apds9960.hpp"
#include "check.hpp"
#include "display_pipeline.hpp"
#include "gesture.hpp"
#include "sim_panel.hpp"

using namespace badge;

namespace {
// Synthetic swipe along one axis: the object's position moves from the
// channel `from` toward the opposite channel.
std::vector<GestureFrame> swipe_frames(char axis, bool positive, int n = 12) {
  std::vector<GestureFrame> v;
  for (int i = 0; i < n; ++i) {
    const int a = 40 + (positive ? i : n - 1 - i) * 15;  // rises
    const int b = 40 + (positive ? n - 1 - i : i) * 15;  // falls
    GestureFrame f{100, 100, 100, 100};
    if (axis == 'v') { f.u = uint8_t(a); f.d = uint8_t(b); }
    else { f.l = uint8_t(a); f.r = uint8_t(b); }
    v.push_back(f);
  }
  return v;
}

// Register-level APDS-9960 model with fault injection.
class FakeApds : public I2cBus {
 public:
  bool present = true;
  uint8_t id = 0xAB;
  int fail_after = -1;  // transactions until the bus dies
  uint8_t regs[256] = {};
  std::vector<GestureFrame> fifo;
  bool engine = false;  // GMODE
  int recoveries = 0;

  bool dead() {
    if (!present) return true;
    if (fail_after == 0) return true;
    if (fail_after > 0) --fail_after;
    return false;
  }
  bool write(uint8_t addr, const uint8_t *d, size_t n) override {
    if (addr != 0x39 || dead() || n < 1) return false;
    for (size_t i = 1; i < n; ++i) regs[uint8_t(d[0] + i - 1)] = d[i];
    if (d[0] == 0xAB && n > 1 && (d[1] & 0x04)) fifo.clear();
    return true;
  }
  bool write_read(uint8_t addr, uint8_t reg, uint8_t *out, size_t n) override {
    if (addr != 0x39 || dead()) return false;
    if (reg == 0x92) { out[0] = id; return true; }
    if (reg == 0xAF) { out[0] = fifo.empty() ? 0 : 1; return true; }
    if (reg == 0xAE) { out[0] = uint8_t(fifo.size() > 32 ? 32 : fifo.size()); return true; }
    if (reg == 0xAB) { out[0] = uint8_t((regs[0xAB] & ~1) | (engine ? 1 : 0)); return true; }
    if (reg == 0xFC) {
      for (size_t i = 0; i < n / 4 && !fifo.empty(); ++i) {
        const GestureFrame f = fifo.front();
        fifo.erase(fifo.begin());
        out[4 * i] = f.u; out[4 * i + 1] = f.d; out[4 * i + 2] = f.l; out[4 * i + 3] = f.r;
      }
      return true;
    }
    for (size_t i = 0; i < n; ++i) out[i] = regs[uint8_t(reg + i)];
    return true;
  }
  void recover() override { ++recoveries; }
  bool ir_enabled() const { return regs[0x80] & 0x01; }
};

// Run one swipe through the driver: frames appear, then the engine exits.
Swipe run_swipe(Apds9960 &s, FakeApds &bus, uint32_t &t, const std::vector<GestureFrame> &frames) {
  Swipe got = Swipe::None;
  bus.engine = true;
  for (size_t i = 0; i < frames.size(); i += 4) {
    for (size_t k = i; k < i + 4 && k < frames.size(); ++k) bus.fifo.push_back(frames[k]);
    t += 11;
    Swipe r = s.poll(t);
    if (r != Swipe::None) got = r;
  }
  bus.engine = false;
  for (int i = 0; i < 5; ++i) {
    t += 11;
    Swipe r = s.poll(t);
    if (r != Swipe::None) got = r;
  }
  return got;
}
}  // namespace

TEST(gesture_decoder_directions) {
  GestureDecoder d;
  struct { char axis; bool pos; Swipe want; } cases[] = {
      {'v', true, Swipe::Down}, {'v', false, Swipe::Up}, {'h', true, Swipe::Right}, {'h', false, Swipe::Left}};
  for (auto &c : cases) {
    d.begin(0);
    for (auto &f : swipe_frames(c.axis, c.pos)) d.add(f);
    SessionResult r = d.finish();
    CHECK(r.raw == c.want);
  }
  // Too few frames, below-noise data and a hand hovering still are rejected.
  d.begin(0);
  d.add({100, 100, 100, 100});
  CHECK(d.finish().raw == Swipe::None);
  d.begin(0);
  for (int i = 0; i < 10; ++i) d.add({5, 5, 5, 5});
  CHECK(d.finish().raw == Swipe::None);
  d.begin(0);
  for (int i = 0; i < 10; ++i) d.add({120, 118, 121, 119});
  CHECK(d.finish().raw == Swipe::None);
}

TEST(gesture_diagonal_is_ambiguous) {
  GestureDecoder d;
  d.begin(0);
  auto v = swipe_frames('v', true), h = swipe_frames('h', true);
  for (size_t i = 0; i < v.size(); ++i) d.add({v[i].u, v[i].d, h[i].l, h[i].r});
  CHECK(d.finish().raw == Swipe::None);
}

TEST(gesture_orientation_mapping) {
  CHECK(orient(Swipe::Up, 0, false) == Swipe::Up);
  CHECK(orient(Swipe::Up, 1, false) == Swipe::Right);
  CHECK(orient(Swipe::Up, 2, false) == Swipe::Down);
  CHECK(orient(Swipe::Up, 3, false) == Swipe::Left);
  CHECK(orient(Swipe::Left, 1, false) == Swipe::Up);
  CHECK(orient(Swipe::Right, 0, true) == Swipe::Left);
  CHECK(orient(Swipe::Up, 0, true) == Swipe::Up);
  CHECK(orient(Swipe::Up, 1, true) == Swipe::Left);
  for (int r = 0; r < 4; ++r)  // rotation is a bijection
    for (int a = 1; a <= 4; ++a)
      for (int b = a + 1; b <= 4; ++b) CHECK(orient(Swipe(a), uint8_t(r), false) != orient(Swipe(b), uint8_t(r), false));
}

TEST(gesture_gate_one_swipe_one_event) {
  SwipeGate g;
  CHECK(g.accept(Swipe::Right, 1000, 700));
  CHECK(!g.accept(Swipe::Left, 1300, 700));  // return stroke of the same hand movement
  CHECK(!g.accept(Swipe::Right, 1650, 700));
  CHECK(g.accept(Swipe::Right, 1700, 700));
  CHECK_EQ(g.suppressed(), 2u);
}

TEST(apds_absent_sensor_is_harmless) {
  FakeApds bus;
  bus.present = false;
  Apds9960 s(bus);
  s.start(0);
  CHECK(s.state() == SensorState::Absent);
  s.set_wanted(true);
  uint32_t t = 0;
  int calls_with_io = 0;
  for (int i = 0; i < 6000; ++i) {  // 60 s of main-loop polling
    t += 10;
    const uint32_t probes = s.stats().probes;
    CHECK(s.poll(t) == Swipe::None);
    calls_with_io += s.stats().probes != probes;
  }
  CHECK(s.state() == SensorState::Absent);
  CHECK(calls_with_io <= 8);  // exponential backoff: ~1,2,4,8,16,30 s retries
  bus.present = true;         // plugged in later
  for (int i = 0; i < 4000 && s.state() != SensorState::Active; ++i) s.poll(t += 10);
  CHECK(s.state() == SensorState::Active);
  CHECK(bus.ir_enabled());
}

TEST(apds_start_forces_power_down_and_mode_toggles_emitter) {
  FakeApds bus;
  bus.regs[0x80] = 0x4D;  // left running by a previous boot (USB soft reset)
  Apds9960 s(bus);
  s.start(0);
  CHECK(s.state() == SensorState::Standby);
  CHECK(!bus.ir_enabled());
  CHECK_EQ(bus.regs[0xA3], 0x41);
  uint32_t t = 0;
  s.set_wanted(true);
  s.poll(t += 10);
  CHECK(s.state() == SensorState::Active);
  CHECK_EQ(bus.regs[0x80], 0x4D);
  s.set_wanted(false);
  s.poll(t += 10);
  CHECK(s.state() == SensorState::Standby);
  CHECK_EQ(bus.regs[0x80], 0x00);  // PON off: engine and IR LED off
  s.set_wanted(true);
  s.poll(t += 10);
  s.shutdown();
  CHECK(!bus.ir_enabled());
  CHECK(!s.wanted());
}

TEST(apds_diagnostic_snapshot_is_nondestructive) {
  FakeApds bus;
  Apds9960 s(bus);
  s.start(0);
  s.set_wanted(true);
  uint32_t t = 0;
  s.poll(t += 10);  // enter Active
  bus.regs[0x9C] = 87;
  bus.engine = true;
  bus.fifo.push_back({11, 22, 33, 44});
  const size_t before = bus.fifo.size();

  const SensorRegisterSnapshot r = s.diagnostic_registers();
  CHECK(r.valid);
  CHECK_EQ(r.enable, 0x4D);
  CHECK_EQ(r.proximity, 87);
  CHECK_EQ(r.gconf4 & 0x01, 1);
  CHECK_EQ(r.gstatus & 0x01, 1);
  CHECK_EQ(r.fifo_level, 1);
  CHECK_EQ(bus.fifo.size(), before);  // diagnostics must not consume GFIFO
}

TEST(gesture_decoder_retains_endpoint_samples_for_diagnostics) {
  GestureDecoder d;
  const GestureFrame first{60, 180, 70, 170};
  const GestureFrame last{180, 60, 170, 70};
  d.begin(0);
  d.add(first);
  d.add({90, 150, 100, 140});
  d.add({120, 120, 120, 120});
  d.add(last);
  const SessionResult r = d.finish();
  CHECK_EQ(r.first.u, first.u);
  CHECK_EQ(r.first.d, first.d);
  CHECK_EQ(r.first.l, first.l);
  CHECK_EQ(r.first.r, first.r);
  CHECK_EQ(r.last.u, last.u);
  CHECK_EQ(r.last.d, last.d);
  CHECK_EQ(r.last.l, last.l);
  CHECK_EQ(r.last.r, last.r);
}

TEST(apds_swipes_end_to_end_with_orientation) {
  FakeApds bus;
  Apds9960 s(bus);
  GestureParams p;
  p.rotation = 1;
  s.set_params(p);
  s.start(0);
  s.set_wanted(true);
  uint32_t t = 0;
  s.poll(t += 10);
  CHECK(run_swipe(s, bus, t, swipe_frames('v', false)) == Swipe::Right);  // raw up, mounted rotated 90
  t += 1000;
  CHECK(run_swipe(s, bus, t, swipe_frames('h', false)) == Swipe::Up);      // raw left
  CHECK_EQ(s.stats().recognized, 2u);
  // The hand's return stroke immediately after is swallowed by the cooldown.
  t += 1000;
  CHECK(run_swipe(s, bus, t, swipe_frames('h', true)) != Swipe::None);
  CHECK(run_swipe(s, bus, t, swipe_frames('h', false)) == Swipe::None);
  CHECK_EQ(s.gate_suppressed(), 1u);
}

TEST(apds_bus_fault_mid_session_recovers) {
  FakeApds bus;
  Apds9960 s(bus);
  s.start(0);
  s.set_wanted(true);
  uint32_t t = 0;
  s.poll(t += 10);
  bus.engine = true;
  for (auto &f : swipe_frames('h', true)) bus.fifo.push_back(f);
  bus.fail_after = 1;  // cable pulled
  for (int i = 0; i < 20; ++i) CHECK(s.poll(t += 10) == Swipe::None);
  CHECK(s.state() == SensorState::Fault);
  CHECK(bus.recoveries >= 1);
  bus.fail_after = -1;  // reconnected
  bus.fifo.clear();
  bus.engine = false;
  for (int i = 0; i < 1000 && s.state() != SensorState::Active; ++i) s.poll(t += 10);
  CHECK(s.state() == SensorState::Active);
  t += 1000;
  CHECK(run_swipe(s, bus, t, swipe_frames('v', true)) == Swipe::Down);
}

TEST(apds_wrong_chip_id_is_not_driven) {
  FakeApds bus;
  bus.id = 0x55;  // some other device at 0x39
  Apds9960 s(bus);
  s.start(0);
  CHECK(s.state() == SensorState::Absent);
  CHECK_EQ(bus.regs[0x80], 0);  // nothing configured
}

TEST(gesture_burst_produces_single_refresh) {
  // Several recognised swipes land while a 4.5 s refresh is running: the
  // scheduler renders only the newest view afterwards.
  std::atomic<uint32_t> clock{0};
  SimPanel panel(&clock);
  static Framebuffer bufs[2];
  static JobQueue jobs;
  static EventQueue events;
  DisplayService display(panel, bufs, 2, jobs, events);
  RenderScheduler sched(bufs, 2, jobs, events);
  display.start(0);
  static int screen = 0;
  auto draw = [](Framebuffer &fb, void *) { fb.clear(Ink::White); fb.fill_rect({int16_t(10 + 60 * screen), 10, 50, 50}, Ink::Black); };
  sched.invalidate();
  for (int i = 0; i < 5; ++i) { clock += 10; sched.poll(draw, nullptr, 0, 0); display.poll(clock); }
  for (int s = 1; s <= 3; ++s) {  // three swipes 700 ms apart during the refresh
    screen = s;
    sched.invalidate();
    for (int i = 0; i < 70; ++i) { clock += 10; sched.poll(draw, nullptr, 0, 0); display.poll(clock); }
  }
  for (int i = 0; i < 3000 && !sched.settled(); ++i) { clock += 10; sched.poll(draw, nullptr, 0, 0); display.poll(clock); }
  int refreshes = 0;
  for (auto &o : panel.ops) refreshes += o.kind == 'F';
  CHECK_EQ(refreshes, 2);  // initial frame + final view only
  CHECK_EQ(panel.violations, 0);
}

// A marginal Qwiic connection: every probe succeeds, but the sensor drops off
// the bus shortly after the engine is enabled. Each ok <-> fault flip changes
// the status indicator and so costs a panel refresh; the flip rate must stay
// bounded by the backoff instead of cycling every second.
TEST(apds_flapping_sensor_is_rate_limited) {
  FakeApds bus;
  Apds9960 s(bus);
  s.set_wanted(true);
  s.start(0);
  uint32_t t = 0;
  auto ok = [&] { return s.state() == SensorState::Standby || s.state() == SensorState::Active; };
  bool was_ok = ok();
  int flips_first = 0, flips_last = 0;
  for (int i = 0; i < 60000; ++i) {  // 10 minutes of 10 ms main-loop polls
    if (s.state() == SensorState::Active && bus.fail_after < 0) bus.fail_after = 3;
    if (s.state() == SensorState::Fault) bus.fail_after = -1;  // answers the next probe
    s.poll(t += 10);
    if (ok() != was_ok) {
      was_ok = !was_ok;
      (i < 30000 ? flips_first : flips_last) += 1;
    }
  }
  CHECK(flips_first > 4);        // it really is flapping
  CHECK(flips_last <= 2 * 10 + 2);  // at most one fault cycle per 30 s backoff
  CHECK(s.stats().faults < 40u);
  // Once the connection is good, a minute of stable operation restores fast
  // retries: the next glitch recovers within about a second.
  bus.fail_after = -1;
  for (int i = 0; i < 7000; ++i) s.poll(t += 10);
  CHECK(s.state() == SensorState::Active);
  bus.fail_after = 0;
  for (int i = 0; i < 5 && s.state() != SensorState::Fault; ++i) s.poll(t += 10);
  CHECK(s.state() == SensorState::Fault);
  bus.fail_after = -1;
  int ms = 0;
  while (s.state() != SensorState::Active && ms < 5000) { s.poll(t += 10); ms += 10; }
  CHECK(s.state() == SensorState::Active);
  CHECK(ms <= 1100);
}
