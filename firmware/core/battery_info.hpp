#pragma once
#include <cstddef>
#include <cstdio>
#include "battery.hpp"

namespace badge {
// Shared with the real Info renderer, tested without Pico hardware.
inline void format_usb_battery_info(char *out, size_t size, const BatteryState &b,
                                    const char *formatted_mv, const char *note) {
  if (b.numeric_valid)
    std::snprintf(out, size, "Power     USB | sense %s (%s)", formatted_mv, note);
  else
    std::snprintf(out, size, "Power     USB | sense unavailable");
}
}  // namespace badge
