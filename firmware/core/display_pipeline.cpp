#include "display_pipeline.hpp"

namespace badge {

const char *refresh_mode_str(RefreshMode m) {
  switch (m) {
    case RefreshMode::None: return "none";
    case RefreshMode::Full: return "full";
    case RefreshMode::Partial: return "partial";
    case RefreshMode::Clean: return "clean";
  }
  return "?";
}

const char *refresh_reason_str(RefreshReason r) {
  switch (r) {
    case RefreshReason::None: return "-";
    case RefreshReason::FirstFrame: return "first frame";
    case RefreshReason::CleanRequested: return "clean requested";
    case RefreshReason::LargeChange: return "large change";
    case RefreshReason::PartialBudget: return "partial budget used";
    case RefreshReason::PartialOff: return "partial disabled";
    case RefreshReason::SmallChange: return "small change";
  }
  return "?";
}

// ---------------------------------------------------------------- display side

void DisplayService::start(uint8_t speed) {
  base_speed_ = speed;
  shown_known_ = false;
  retry_backoff_ms_ = kPanelRetryMinMs;
  retry_armed_ = false;
  if (panel_.init(speed)) {
    state_ = State::Idle;
    panel_ok_ = true;
  } else {
    ++stats_.panel_faults;
    state_ = State::PanelFault;  // retry timer is armed on the first poll
    panel_ok_ = false;
  }
}

void DisplayService::panel_failed(uint32_t now_ms) {
  ++stats_.panel_faults;
  shown_known_ = false;
  state_ = State::PanelFault;
  panel_ok_ = false;
  retry_at_ms_ = now_ms + retry_backoff_ms_;
  retry_armed_ = true;
  retry_backoff_ms_ = retry_backoff_ms_ >= kPanelRetryMaxMs / 2 ? kPanelRetryMaxMs : retry_backoff_ms_ * 2;
}

// Controller unresponsive: never touch it except for a bounded re-init now
// and then. Jobs are released at once (as if shown) so core 0 never waits
// for a refresh that cannot happen; recovery forces one clean redraw.
void DisplayService::poll_fault(uint32_t now_ms) {
  FrameJob job{};
  while (jobs_.pop(&job)) {
    ++stats_.dropped;
    emit(DisplayEventKind::BufferReleased, job, RefreshMode::None, 0);
    emit(DisplayEventKind::Done, job, RefreshMode::None, 0);
  }
  if (!retry_armed_) {
    retry_at_ms_ = now_ms + retry_backoff_ms_;
    retry_armed_ = true;
    return;
  }
  if (int32_t(now_ms - retry_at_ms_) < 0) return;
  if (!panel_.init(base_speed_)) {
    panel_failed(now_ms);
    return;
  }
  state_ = State::Idle;
  panel_ok_ = true;
  retry_armed_ = false;
  retry_backoff_ms_ = kPanelRetryMinMs;
  emit(DisplayEventKind::PanelReset, job, RefreshMode::None, 0);
}

void DisplayService::emit(DisplayEventKind k, const FrameJob &j, RefreshMode m, uint32_t dur) {
  DisplayEvent e{k, m, j.buffer, k == DisplayEventKind::Done ? active_reason_ : RefreshReason::None, j.seq, dur};
  // Sized so this cannot fail: with the one-pending-job rule a single poll
  // emits at most three events and core 0 drains the queue every loop. Never
  // spin here - in single-core mode the consumer runs on this same core.
  if (!events_.push(e)) ++stats_.event_overflows;
}

void DisplayService::begin(const FrameJob &j, uint32_t now_ms) {
  const Framebuffer &fb = buffers_[j.buffer];
  RefreshMode mode = RefreshMode::Full;
  RefreshReason reason = RefreshReason::FirstFrame;
  Rect diff{};
  if (j.clean) {
    mode = RefreshMode::Clean;
    reason = RefreshReason::CleanRequested;
  } else if (shown_known_) {
    diff = fb.diff_bounds(shown_);
    if (diff.empty()) {
      ++stats_.suppressed;
      emit(DisplayEventKind::Suppressed, j, RefreshMode::None, 0);
      emit(DisplayEventKind::BufferReleased, j, RefreshMode::None, 0);
      return;
    }
    const uint32_t area = uint32_t(diff.w) * uint32_t(diff.h);
    const uint32_t full = uint32_t(Framebuffer::kWidth) * Framebuffer::kHeight;
    if (area * 100 > full * kPartialMaxAreaPct) reason = RefreshReason::LargeChange;
    else if (j.max_partials == 0) reason = RefreshReason::PartialOff;
    else if (partials_since_full_ >= j.max_partials) reason = RefreshReason::PartialBudget;
    else {
      mode = RefreshMode::Partial;
      reason = RefreshReason::SmallChange;
    }
  }
  shown_.copy_from(fb);
  // The job buffer is no longer needed: hand it back before the slow part.
  emit(DisplayEventKind::BufferReleased, j, mode, 0);

  const uint8_t want_speed = mode == RefreshMode::Clean ? 0 : j.speed;
  base_speed_ = j.speed;
  if (panel_.speed() != want_speed && !panel_.set_speed(want_speed)) {
    panel_failed(now_ms);
    emit(DisplayEventKind::Done, j, RefreshMode::None, 0);  // never shown
    return;
  }
  if (mode == RefreshMode::Partial) {
    panel_.start_partial(shown_, diff);
    ++partials_since_full_;
    ++stats_.partial;
  } else {
    panel_.start_full(shown_);
    partials_since_full_ = 0;
    if (mode == RefreshMode::Clean) ++stats_.clean; else ++stats_.full;
  }
  shown_known_ = true;
  active_ = j;
  active_mode_ = mode;
  active_reason_ = reason;
  started_ms_ = now_ms;
  state_ = State::Refreshing;
}

bool DisplayService::poll(uint32_t now_ms) {
  heartbeat_.store(heartbeat_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
  if (state_ == State::PanelFault) {
    poll_fault(now_ms);
    return false;
  }
  if (state_ == State::Refreshing) {
    if (panel_.busy()) {
      if (now_ms - started_ms_ > kBusyTimeoutMs) {
        ++stats_.timeouts;
        emit(DisplayEventKind::Timeout, active_, active_mode_, now_ms - started_ms_);
        shown_known_ = false;  // image state unknown: next job is a full refresh
        state_ = State::Idle;
        // Hardware reset of the controller. Bounded: if BUSY stays low this
        // is a panel fault, not a hang of the display context.
        if (!panel_.init(base_speed_)) panel_failed(now_ms);
      }
      return true;
    }
    panel_.finish();
    const uint32_t dur = now_ms - started_ms_;
    stats_.last_ms = dur;
    if (dur > stats_.max_ms) stats_.max_ms = dur;
    state_ = State::Idle;
    emit(DisplayEventKind::Done, active_, active_mode_, dur);
  }
  // Idle: take the newest job, dropping any obsolete ones queued before it.
  FrameJob job, newer;
  if (!jobs_.pop(&job)) return false;
  while (jobs_.pop(&newer)) {
    ++stats_.dropped;
    emit(DisplayEventKind::Dropped, job, RefreshMode::None, 0);
    emit(DisplayEventKind::BufferReleased, job, RefreshMode::None, 0);
    job = newer;
  }
  begin(job, now_ms);
  return state_ == State::Refreshing;
}

// ----------------------------------------------------------------- app side

int RenderScheduler::free_count() const {
  int n = 0;
  for (int i = 0; i < nbuf_; ++i) n += owned_[i];
  return n;
}

int RenderScheduler::trace(RefreshRecord *out, int max) const {
  const int n = trace_count_ < kTrace ? trace_count_ : kTrace;
  const int k = n < max ? n : max;
  for (int i = 0; i < k; ++i) out[i] = trace_[(trace_count_ - k + i) % kTrace];
  return k;
}

int RenderScheduler::poll(RenderFn fn, void *ctx, uint8_t speed, uint8_t max_partials, uint32_t now_ms) {
  int seen = 0;
  now_ = now_ms;
  DisplayEvent e;
  while (events_.pop(&e)) {
    ++seen;
    if (e.kind == DisplayEventKind::Done || e.kind == DisplayEventKind::Suppressed ||
        e.kind == DisplayEventKind::Timeout) {
      for (int i = 0; i < kTrace && i < trace_count_; ++i) {
        RefreshRecord &r = trace_[(trace_count_ - 1 - i) % kTrace];
        if (r.seq == e.seq) {
          r.done_ms = now_ms;
          r.busy_ms = e.duration_ms;
          r.mode = e.mode;
          r.reason = e.reason;
          break;
        }
      }
    }
    switch (e.kind) {
      case DisplayEventKind::BufferReleased:
        if (e.buffer < nbuf_) owned_[e.buffer] = true;
        if (int32_t(e.seq - accepted_seq_) > 0) accepted_seq_ = e.seq;
        break;
      case DisplayEventKind::Done:
      case DisplayEventKind::Suppressed:
      case DisplayEventKind::Dropped:
        if (int32_t(e.seq - done_seq_) > 0 && e.kind != DisplayEventKind::Dropped) done_seq_ = e.seq;
        break;
      case DisplayEventKind::Timeout:
        ++timeouts_;
        if (int32_t(e.seq - done_seq_) > 0) done_seq_ = e.seq;
        // The panel was reset and its content is unknown: redraw everything.
        have_last_ = false;
        dirty_ = true;
        clean_ = true;
        break;
      case DisplayEventKind::PanelReset:
        // Recovered from a panel fault: whatever was rendered meanwhile was
        // never shown, so the same frame must not be suppressed by hash.
        have_last_ = false;
        dirty_ = true;
        clean_ = true;
        break;
    }
  }
  if (!dirty_) return seen;
  // Render only once the previous frame is fully on the panel: requests that
  // arrive during a refresh (seconds) collapse into one frame of the newest
  // view. Rendering takes milliseconds, so waiting costs nothing visible.
  if (done_seq_ != submitted_seq_) return seen;
  int buf = -1;
  for (int i = 0; i < nbuf_; ++i)
    if (owned_[i]) { buf = i; break; }
  if (buf < 0) return seen;

  Framebuffer &fb = buffers_[buf];
  fb.reset_clip();
  fn(fb, ctx);
  fb.reset_clip();
  const uint32_t h = fb.hash();
  const bool clean = clean_;
  dirty_ = false;
  clean_ = false;
  if (!clean && have_last_ && h == last_hash_) {
    ++suppressed_;  // unchanged screen: no job, no refresh
    return seen;
  }
  FrameJob job{uint8_t(buf), speed, max_partials, uint8_t(clean), submitted_seq_ + 1};
  if (!jobs_.push(job)) {
    dirty_ = true;  // cannot happen with the one-pending rule; retry next poll
    clean_ = clean;
    return seen;
  }
  owned_[buf] = false;
  ++submitted_seq_;
  trace_[trace_count_ % kTrace] = {submitted_seq_, request_ms_, now_ms, 0, 0, RefreshMode::None,
                                   RefreshReason::None, speed, uint8_t(clean)};
  ++trace_count_;
  last_hash_ = h;
  have_last_ = true;
  return seen;
}

}  // namespace badge
