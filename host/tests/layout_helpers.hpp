// Small helpers shared by the per-target layout tests (tests/<target>/).
#pragma once

#include <string>

#include "check.hpp"
#include "renderer.hpp"

namespace layout_test {

inline void set(badge::Settings &s, const std::string &k, const char *v) {
  CHECK(badge::settings_set(&s, *badge::find_field(k.c_str()), v) == badge::SetResult::Ok);
}

inline badge::RenderContext context(badge::Settings &s) {
  badge::RenderContext c;
  c.settings = &s;
  return c;
}

// The first `n` contacts set to a short web line, the rest cleared.
inline void contacts(badge::Settings &s, int n) {
  for (int i = 1; i <= badge::kMaxContacts; ++i) {
    set(s, "contact" + std::to_string(i) + ".label", i <= n ? "Web" : "");
    set(s, "contact" + std::to_string(i) + ".value", i <= n ? "example.com" : "");
  }
}

// A card with every contact line under a two-line title and an affiliation.
inline void crowded_card(badge::Settings &s) {
  badge::settings_defaults(&s);
  contacts(s, badge::kMaxContacts);
  set(s, "title", "Principal Instrumentation Engineer");
  set(s, "affiliation", "Example Laboratories");
}

// The card with five contact lines and a QR caption.
inline void captioned_card(badge::Settings &s) {
  badge::settings_defaults(&s);
  set(s, "qr.payload", "https://example.com/alex");
  set(s, "qr.caption", "Scan to save my contact");
  contacts(s, 5);
}

}  // namespace layout_test
