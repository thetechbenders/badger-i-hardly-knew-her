// Render/display pipeline shared by the dual-core and single-core modes.
//
//   core 0 (app)                               display context (core 1, or core 0
//   ------------                               in single-core diagnostic mode)
//   RenderScheduler                            DisplayService
//     owns: job buffers while Free               owns: Panel (SPI, BUSY, RESET, DC, CS),
//     renders the newest desired view             the "shown" image, refresh policy
//     -- FrameJob{buffer, seq, ...} -->  JobQueue (SPSC, depth 4)
//     <-- DisplayEvent{...} ----------  EventQueue (SPSC, depth 8)
//
// Buffer ownership moves with the FrameJob and comes back with the
// BufferReleased event; neither side touches a buffer it does not own.
// The Pimoroni UC8151 driver has no locking and busy-waits internally, so
// every Panel call happens only inside DisplayService (one context).
#pragma once

#include <atomic>
#include <cstdint>

#include "framebuffer.hpp"
#include "spsc_queue.hpp"

namespace badge {

enum class RefreshMode : uint8_t { None = 0, Full, Partial, Clean };
const char *refresh_mode_str(RefreshMode m);

struct FrameJob {
  uint8_t buffer;
  uint8_t speed;          // configured update speed (0..3)
  uint8_t max_partials;   // 0 disables partial refresh
  uint8_t clean;          // force a full refresh with the OTP (cleanest) waveform
  uint32_t seq;
};

enum class DisplayEventKind : uint8_t {
  BufferReleased,  // job accepted (or dropped); buffer ownership returns to core 0
  Done,            // refresh complete, panel powered down
  Suppressed,      // identical to the shown image; no refresh
  Dropped,         // superseded by a newer job before it started
  Timeout,         // BUSY never cleared; panel re-initialised
  PanelReset,      // panel (re)initialised after a fault: content unknown, redraw
};

// Why the display service chose a refresh mode (diagnostics: `diag refresh`).
enum class RefreshReason : uint8_t {
  None = 0,
  FirstFrame,     // panel content unknown (boot, after a fault): full
  CleanRequested, // A long press / refresh clean / after a fault: OTP waveform
  LargeChange,    // changed area >= kPartialMaxAreaPct: full
  PartialBudget,  // refresh.max_partials partials since the last full: full
  PartialOff,     // partial refresh disabled: full
  SmallChange,    // partial window
};
const char *refresh_reason_str(RefreshReason r);

struct DisplayEvent {
  DisplayEventKind kind;
  RefreshMode mode;
  uint8_t buffer;
  RefreshReason reason;  // for Done
  uint32_t seq;
  uint32_t duration_ms;
};

using JobQueue = SpscQueue<FrameJob, 4>;
using EventQueue = SpscQueue<DisplayEvent, 8>;

class Panel {
 public:
  virtual ~Panel() = default;
  // Reset + configure. Must be bounded in time: returns false if the
  // controller does not come out of reset (BUSY held low).
  virtual bool init(uint8_t speed) = 0;
  virtual bool set_speed(uint8_t speed) = 0;  // same contract as init()
  virtual uint8_t speed() const = 0;
  virtual bool busy() = 0;
  virtual void start_full(const Framebuffer &fb) = 0;
  virtual void start_partial(const Framebuffer &fb, Rect r) = 0;
  virtual void finish() = 0;  // power the booster off after a refresh
  virtual uint32_t expected_ms() const = 0;
};

struct DisplayStats {
  uint32_t full = 0, partial = 0, clean = 0, suppressed = 0, dropped = 0, timeouts = 0;
  uint32_t last_ms = 0, max_ms = 0;
  uint32_t event_overflows = 0;  // must stay 0; checked by diagnostics and tests
  uint32_t panel_faults = 0;     // init/reset failures (controller unresponsive)
};

class DisplayService {
 public:
  static constexpr uint32_t kPartialMaxAreaPct = 40;
  static constexpr uint32_t kBusyTimeoutMs = 15000;
  static constexpr uint32_t kPanelRetryMinMs = 2000;   // re-init backoff after a panel fault
  static constexpr uint32_t kPanelRetryMaxMs = 60000;

  DisplayService(Panel &panel, Framebuffer *buffers, int nbuf, JobQueue &jobs, EventQueue &events)
      : panel_(panel), buffers_(buffers), nbuf_(nbuf), jobs_(jobs), events_(events) {}

  void start(uint8_t speed);
  // Non-blocking step. Returns true while a refresh is in progress.
  bool poll(uint32_t now_ms);
  bool busy() const { return state_ == State::Refreshing; }
  // False while the controller is unresponsive. Jobs are then released
  // without touching the panel, so the app keeps running (buttons, USB,
  // sleep), and re-initialisation is retried with backoff.
  bool panel_ok() const { return panel_ok_.load(std::memory_order_relaxed); }
  const DisplayStats &stats() const { return stats_; }
  // Incremented on every poll, from the display context; read by core 0 for
  // watchdog liveness, hence atomic.
  uint32_t heartbeat() const { return heartbeat_.load(std::memory_order_relaxed); }
  // For tests / diagnostics: the image most recently sent to the panel.
  const Framebuffer &shown() const { return shown_; }

 private:
  enum class State : uint8_t { Idle, Refreshing, PanelFault };
  void emit(DisplayEventKind k, const FrameJob &j, RefreshMode m, uint32_t dur);
  void begin(const FrameJob &j, uint32_t now_ms);
  void panel_failed(uint32_t now_ms);
  void poll_fault(uint32_t now_ms);

  Panel &panel_;
  Framebuffer *buffers_;
  int nbuf_;
  JobQueue &jobs_;
  EventQueue &events_;
  Framebuffer shown_;
  bool shown_known_ = false;  // panel content unknown until the first full refresh
  State state_ = State::Idle;
  FrameJob active_{};
  RefreshMode active_mode_ = RefreshMode::None;
  RefreshReason active_reason_ = RefreshReason::None;
  uint32_t started_ms_ = 0;
  uint8_t partials_since_full_ = 0;
  uint8_t base_speed_ = 1;
  bool retry_armed_ = false;
  uint32_t retry_at_ms_ = 0, retry_backoff_ms_ = kPanelRetryMinMs;
  DisplayStats stats_;
  std::atomic<uint32_t> heartbeat_{0};
  std::atomic<bool> panel_ok_{true};  // mirrors state_ != PanelFault for other cores
};

// Renders the newest desired view once the previous frame has finished on
// the panel: however many requests arrive during a refresh, exactly one more
// frame (the newest view) follows it. Obsolete views are never rendered.
class RenderScheduler {
 public:
  using RenderFn = void (*)(Framebuffer &fb, void *ctx);

  RenderScheduler(Framebuffer *buffers, int nbuf, JobQueue &jobs, EventQueue &events)
      : buffers_(buffers), nbuf_(nbuf), jobs_(jobs), events_(events) {}

  // Mark the screen dirty. `clean` requests a full clean refresh.
  void invalidate(bool clean = false) {
    if (dirty_) ++coalesced_;  // an unrendered view is superseded
    else request_ms_ = now_;  // first request of this frame (time of the last poll)
    dirty_ = true;
    clean_ |= clean;
  }
  // Drain display events; render + submit if possible. Returns events seen.
  // `now_ms` only timestamps the refresh trace.
  int poll(RenderFn fn, void *ctx, uint8_t speed, uint8_t max_partials, uint32_t now_ms = 0);
  // True when the newest view has been rendered and fully shown.
  bool settled() const { return !dirty_ && done_seq_ == submitted_seq_ && free_count() == nbuf_; }
  bool display_active() const { return done_seq_ != submitted_seq_; }
  int free_count() const;
  uint32_t coalesced() const { return coalesced_; }
  uint32_t suppressed() const { return suppressed_; }
  uint32_t submitted() const { return submitted_seq_; }
  uint32_t timeouts() const { return timeouts_; }

  // Last kTrace submitted frames, oldest first: when they were requested,
  // submitted and finished, the mode/reason the display service chose and
  // how long the panel was busy. Recorded on core 0 from the event queue.
  struct RefreshRecord {
    uint32_t seq, request_ms, submit_ms, done_ms, busy_ms;
    RefreshMode mode;
    RefreshReason reason;
    uint8_t speed;
    uint8_t clean_requested;
  };
  static constexpr int kTrace = 16;
  int trace(RefreshRecord *out, int max) const;

 private:
  RefreshRecord trace_[kTrace] = {};
  int trace_count_ = 0;  // total recorded (ring index = count % kTrace)
  uint32_t request_ms_ = 0;
  uint32_t now_ = 0;
  Framebuffer *buffers_;
  int nbuf_;
  JobQueue &jobs_;
  EventQueue &events_;
  bool owned_[4] = {true, true, true, true};  // true = core 0 owns (free)
  bool dirty_ = false, clean_ = false;
  bool have_last_ = false;
  uint32_t last_hash_ = 0;
  uint32_t submitted_seq_ = 0, accepted_seq_ = 0, done_seq_ = 0;
  uint32_t coalesced_ = 0, suppressed_ = 0, timeouts_ = 0;
};

}  // namespace badge
