#pragma once
#include "framebuffer.hpp"
#include "display_pipeline.hpp"
namespace badge {
// Compiled privately; never part of Settings or a USB field.
struct Admission {
  const char *payload;
  const char *event;
  const char *holder;
  uint16_t timeout_s;
};
// Shared by timeout dismissal, sleep and reboot. A drained fault queue is
// insufficient evidence: only a newer successful clean frame can release it.
class AdmissionConcealment {
 public:
  void begin(uint32_t submitted) {
    if (!pending_) after_ = submitted;
    pending_ = true;
  }
  bool pending() const { return pending_; }
  bool complete(const RenderScheduler &scheduler, bool panel_ok, bool busy) {
    if (!pending_ || !panel_ok || busy || !scheduler.clean_completed_after(after_)) return false;
    pending_ = false;
    return true;
  }
 private:
  bool pending_ = false;
  uint32_t after_ = 0;
};
extern const Admission *const kPrivateAdmission;
bool render_admission(Framebuffer &fb, const Admission *admission);
}  // namespace badge
