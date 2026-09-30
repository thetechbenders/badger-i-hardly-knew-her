#include "apds9960.hpp"

namespace badge {

namespace {
// Register map (Broadcom APDS-9960 datasheet).
enum : uint8_t {
  ENABLE = 0x80, ATIME = 0x81, WTIME = 0x83, PILT = 0x89, PIHT = 0x8B, PERS = 0x8C,
  CONFIG1 = 0x8D, PPULSE = 0x8E, CONTROL = 0x8F, CONFIG2 = 0x90, ID = 0x92,
  POFFSET_UR = 0x9D, POFFSET_DL = 0x9E, CONFIG3 = 0x9F,
  GPENTH = 0xA0, GEXTH = 0xA1, GCONF1 = 0xA2, GCONF2 = 0xA3, GOFFSET_U = 0xA4, GOFFSET_D = 0xA5,
  GPULSE = 0xA6, GOFFSET_L = 0xA7, GOFFSET_R = 0xA9, GCONF3 = 0xAA, GCONF4 = 0xAB,
  GFLVL = 0xAE, GSTATUS = 0xAF, GFIFO_U = 0xFC,
};
enum : uint8_t { EN_PON = 0x01, EN_PEN = 0x04, EN_WEN = 0x08, EN_GEN = 0x40 };
enum : uint8_t { GSTATUS_GVALID = 0x01, GSTATUS_GFOV = 0x02, GCONF4_GMODE = 0x01, GCONF4_GFIFO_CLR = 0x04 };

// Values follow the SparkFun APDS-9960 library defaults for gesture use.
struct RegVal { uint8_t reg, val; };
constexpr RegVal kConfig[] = {
    {ENABLE, 0x00},     {ATIME, 219},       {WTIME, 0xFF},     {PPULSE, 0x87},
    {POFFSET_UR, 0},    {POFFSET_DL, 0},    {CONFIG1, 0x60},   {CONTROL, 0x09},  // LDRIVE 100 mA, PGAIN 4x, AGAIN 4x
    {PILT, 0},          {PIHT, 50},         {PERS, 0x11},      {CONFIG2, 0x31},  // LED boost 300 %
    {CONFIG3, 0},       {GPENTH, 40},       {GEXTH, 30},       {GCONF1, 0x40},   // FIFO threshold 4 datasets
    {GCONF2, 0x41},     {GOFFSET_U, 0},     {GOFFSET_D, 0},    {GOFFSET_L, 0},   // GGAIN 4x, GLDRIVE 100 mA, GWTIME 2.8 ms
    {GOFFSET_R, 0},     {GPULSE, 0xC9},     {GCONF3, 0},       {GCONF4, GCONF4_GFIFO_CLR},  // 32 us x 10 pulses
};
}  // namespace

const char *sensor_state_str(SensorState s) {
  switch (s) {
    case SensorState::Unprobed: return "not probed";
    case SensorState::Absent: return "not detected";
    case SensorState::Standby: return "standby (off)";
    case SensorState::Active: return "active";
    case SensorState::Fault: return "fault";
  }
  return "?";
}

void Apds9960::set_params(const GestureParams &p) {
  params_ = p;
  decoder_.set_params(p);
}

bool Apds9960::reg_write(uint8_t reg, uint8_t v) {
  const uint8_t b[2] = {reg, v};
  if (bus_.write(kAddr, b, 2)) { consec_errors_ = 0; return true; }
  ++stats_.i2c_errors;
  ++consec_errors_;
  return false;
}

bool Apds9960::reg_read(uint8_t reg, uint8_t *v) {
  if (bus_.write_read(kAddr, reg, v, 1)) { consec_errors_ = 0; return true; }
  ++stats_.i2c_errors;
  ++consec_errors_;
  return false;
}

bool Apds9960::probe_and_configure() {
  ++stats_.probes;
  uint8_t id = 0;
  if (!reg_read(ID, &id)) return false;
  stats_.chip_id = id;
  // 0xAB: APDS-9960. 0xA8 / 0x9C are reported by some compatible parts.
  if (id != 0xAB && id != 0xA8 && id != 0x9C) return false;
  for (const RegVal &rv : kConfig)
    if (!reg_write(rv.reg, rv.val)) return false;
  uint8_t check = 0;
  if (!reg_read(GCONF2, &check) || check != 0x41) return false;
  return true;
}

bool Apds9960::power(bool on) {
  if (on) return reg_write(GCONF4, GCONF4_GFIFO_CLR) && reg_write(ENABLE, EN_PON | EN_WEN | EN_PEN | EN_GEN);
  // PON = 0: oscillator, engines and the IR LED driver off (sleep state).
  const bool a = reg_write(ENABLE, 0x00);
  const bool b = reg_write(GCONF4, GCONF4_GFIFO_CLR);
  return a && b;
}

void Apds9960::schedule_retry(uint32_t now_ms) {
  next_retry_ms_ = now_ms + backoff_ms_;
  backoff_ms_ = backoff_ms_ >= 15000 ? 30000 : backoff_ms_ * 2;
}

void Apds9960::io_error(uint32_t now_ms) {
  if (consec_errors_ < 3) return;
  ++stats_.faults;
  if (decoder_.active()) decoder_.finish();
  bus_.recover();
  const uint8_t b[2] = {ENABLE, 0x00};
  (void)bus_.write(kAddr, b, 2);  // best effort: turn the IR LED off
  consec_errors_ = 0;
  state_ = SensorState::Fault;
  schedule_retry(now_ms);
}

void Apds9960::start(uint32_t now_ms) {
  if (probe_and_configure() && power(false)) {
    state_ = SensorState::Standby;
    backoff_ms_ = 1000;
  } else {
    state_ = SensorState::Absent;
    consec_errors_ = 0;
    schedule_retry(now_ms);
  }
}

void Apds9960::set_wanted(bool on) { wanted_ = on; }

void Apds9960::shutdown() {
  if (state_ == SensorState::Active || state_ == SensorState::Standby) {
    (void)power(false);
    if (state_ == SensorState::Active) state_ = SensorState::Standby;
  }
  wanted_ = false;
}

Swipe Apds9960::poll(uint32_t now_ms) {
  switch (state_) {
    case SensorState::Unprobed:
      start(now_ms);
      return Swipe::None;
    case SensorState::Absent:
    case SensorState::Fault:
      if (!wanted_ || int32_t(now_ms - next_retry_ms_) < 0) return Swipe::None;
      if (probe_and_configure() && power(false)) {
        state_ = SensorState::Standby;
        backoff_ms_ = 1000;
      } else {
        consec_errors_ = 0;
        schedule_retry(now_ms);
      }
      return Swipe::None;
    case SensorState::Standby:
      if (!wanted_) return Swipe::None;
      if (power(true)) {
        state_ = SensorState::Active;
        next_poll_ms_ = now_ms;
      } else {
        io_error(now_ms);
      }
      return Swipe::None;
    case SensorState::Active:
      break;
  }
  if (!wanted_) {
    if (decoder_.active()) decoder_.finish();
    if (power(false)) state_ = SensorState::Standby;
    else io_error(now_ms);
    return Swipe::None;
  }
  if (!decoder_.active() && int32_t(now_ms - next_poll_ms_) < 0) return Swipe::None;
  next_poll_ms_ = now_ms + kPollMs;

  uint8_t gstatus;
  if (!reg_read(GSTATUS, &gstatus)) { io_error(now_ms); return Swipe::None; }
  if (gstatus & GSTATUS_GFOV) {
    ++stats_.overflows;
    if (decoder_.active()) decoder_.finish();  // lost data: drop the session
    if (!reg_write(GCONF4, GCONF4_GFIFO_CLR)) io_error(now_ms);
    return Swipe::None;
  }
  if (gstatus & GSTATUS_GVALID) {
    uint8_t level;
    if (!reg_read(GFLVL, &level)) { io_error(now_ms); return Swipe::None; }
    if (level > 32) level = 32;
    uint8_t buf[128];
    if (level && !bus_.write_read(kAddr, GFIFO_U, buf, size_t(level) * 4)) {
      ++stats_.i2c_errors;
      ++consec_errors_;
      io_error(now_ms);
      return Swipe::None;
    }
    consec_errors_ = 0;
    if (!decoder_.active()) decoder_.begin(now_ms);
    for (int i = 0; i < level; ++i) decoder_.add({buf[4 * i], buf[4 * i + 1], buf[4 * i + 2], buf[4 * i + 3]});
    if (now_ms - decoder_.started_ms() <= params_.max_session_ms) return Swipe::None;
  } else if (!decoder_.active()) {
    return Swipe::None;
  } else {
    uint8_t g4;
    if (!reg_read(GCONF4, &g4)) { io_error(now_ms); return Swipe::None; }
    if ((g4 & GCONF4_GMODE) && now_ms - decoder_.started_ms() <= params_.max_session_ms) return Swipe::None;
  }
  // Session over (engine exited) or too long (hovering).
  const bool too_long = now_ms - decoder_.started_ms() > params_.max_session_ms;
  SessionResult r = decoder_.finish();
  ++stats_.sessions;
  stats_.last = r;
  if (too_long || r.raw == Swipe::None) {
    ++stats_.rejected;
    if (too_long) (void)reg_write(GCONF4, GCONF4_GFIFO_CLR);
    return Swipe::None;
  }
  const Swipe s = orient(r.raw, params_.rotation, params_.mirror);
  if (!gate_.accept(s, now_ms, params_.cooldown_ms)) return Swipe::None;
  ++stats_.recognized;
  stats_.last_swipe = s;
  return s;
}

}  // namespace badge
