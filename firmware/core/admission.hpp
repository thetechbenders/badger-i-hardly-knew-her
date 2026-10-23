#pragma once
#include "framebuffer.hpp"
namespace badge {
// Compiled privately; never part of Settings or a USB field.
struct Admission {
  const char *payload;
  const char *event;
  const char *holder;
  uint16_t timeout_s;
};
extern const Admission *const kPrivateAdmission;
bool render_admission(Framebuffer &fb, const Admission *admission);
}  // namespace badge
