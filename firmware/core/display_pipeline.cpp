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

// ---------------------------------------------------------------- display side

void DisplayService::start(uint8_t speed) {
  base_speed_ = speed;
  panel_.init(speed);
  shown_known_ = false;
  state_ = State::Idle;
}

void DisplayService::emit(DisplayEventKind k, const FrameJob &j, RefreshMode m, uint32_t dur) {
  DisplayEvent e{k, m, j.buffer, 0, j.seq, dur};
  // Sized so this cannot fail: with the one-pending-job rule a single poll
  // emits at most three events and core 0 drains the queue every loop. Never
  // spin here - in single-core mode the consumer runs on this same core.
  if (!events_.push(e)) ++stats_.event_overflows;
}

void DisplayService::begin(const FrameJob &j, uint32_t now_ms) {
  const Framebuffer &fb = buffers_[j.buffer];
  RefreshMode mode = RefreshMode::Full;
  Rect diff{};
  if (j.clean) {
    mode = RefreshMode::Clean;
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
    if (j.max_partials > 0 && partials_since_full_ < j.max_partials &&
        area * 100 <= full * kPartialMaxAreaPct) {
      mode = RefreshMode::Partial;
    }
  }
  shown_.copy_from(fb);
  // The job buffer is no longer needed: hand it back before the slow part.
  emit(DisplayEventKind::BufferReleased, j, mode, 0);

  const uint8_t want_speed = mode == RefreshMode::Clean ? 0 : j.speed;
  base_speed_ = j.speed;
  if (panel_.speed() != want_speed) panel_.set_speed(want_speed);
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
  started_ms_ = now_ms;
  state_ = State::Refreshing;
}

bool DisplayService::poll(uint32_t now_ms) {
  ++stats_.heartbeat;
  if (state_ == State::Refreshing) {
    if (panel_.busy()) {
      if (now_ms - started_ms_ > kBusyTimeoutMs) {
        ++stats_.timeouts;
        panel_.init(base_speed_);  // hardware reset of the controller
        shown_known_ = false;      // image state unknown: next job is a full refresh
        state_ = State::Idle;
        emit(DisplayEventKind::Timeout, active_, active_mode_, now_ms - started_ms_);
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

int RenderScheduler::poll(RenderFn fn, void *ctx, uint8_t speed, uint8_t max_partials) {
  int seen = 0;
  DisplayEvent e;
  while (events_.pop(&e)) {
    ++seen;
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
  last_hash_ = h;
  have_last_ = true;
  return seen;
}

}  // namespace badge
