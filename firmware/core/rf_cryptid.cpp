#include "rf_cryptid.hpp"
#include <cstdio>
#include <cstring>
namespace badge {
namespace {
uint8_t tier(unsigned n) { return n < 3 ? 0 : n < 6 ? 1 : n < 11 ? 2 : n < 21 ? 3 : 4; }
uint8_t bin_channel(uint8_t ch) { return ch <= 5 ? 0 : ch <= 10 ? 1 : 2; }
uint32_t mix(uint32_t h, uint8_t v) { return (h ^ v) * 16777619u; }
bool same_mac(const RfObservation &a, const RfObservation &b) {
  return std::memcmp(a.bssid, b.bssid, 6) == 0;
}
}  // namespace
RfFingerprint rf_fingerprint(const RfObservation *rows, size_t n) {
  RfFingerprint f;
  if (!rows) n = 0;
  // Only results for 2.4 GHz channels 1..14 can influence a specimen.
  // Repeated observations of one BSSID are one organism signal; strongest wins.
  for (size_t i = 0; i < n; ++i) {
    if (rows[i].channel < 1 || rows[i].channel > 14) continue;
    bool duplicate = false;
    for (size_t j = 0; j < i; ++j)
      if (rows[j].channel >= 1 && rows[j].channel <= 14 && same_mac(rows[i], rows[j])) {
        // Only the strongest copy of a repeated BSSID contributes. Tie
        // breaking is stable, independent of arrival order.
        if (rows[j].rssi > rows[i].rssi ||
            (rows[j].rssi == rows[i].rssi && j < i)) { duplicate = true; break; }
      }
    if (duplicate) continue;
    // Skip any earlier weaker copies of this BSSID (another pass below).
    bool later_stronger = false;
    for (size_t j = i + 1; j < n; ++j)
      if (same_mac(rows[i], rows[j]) && rows[j].channel >= 1 && rows[j].channel <= 14 &&
          (rows[j].rssi > rows[i].rssi ||
           (rows[j].rssi == rows[i].rssi && rows[j].channel < rows[i].channel))) {
        later_stronger = true; break;
      }
    if (later_stronger) continue;
    if (f.count == 255) break;
    ++f.count;
    if (rows[i].rssi >= -60) ++f.strong;
    else if (rows[i].rssi >= -78) ++f.medium;
    else ++f.weak;
    if (rows[i].hidden) ++f.hidden;
    if (!rows[i].secured) ++f.open;
    ++f.band[bin_channel(rows[i].channel)];
    if (!f.channel_span) f.channel_span = rows[i].channel; // temporary min stored below
  }
  uint8_t lo = 15, hi = 0;
  for (size_t i = 0; i < n; ++i) {
    if (rows[i].channel >= 1 && rows[i].channel <= 14) {
      // Excluding a duplicate channel is unnecessary for coarse min/max span.
      if (rows[i].channel < lo) lo = rows[i].channel;
      if (rows[i].channel > hi) hi = rows[i].channel;
    }
  }
  f.channel_span = hi >= lo ? hi - lo : 0;
  // Identity uses coarse ecological categories rather than MAC hashes or
  // exact RSSI. Minor fluctuations generally preserve the plate's identity.
  uint32_t h = 2166136261u;
  h = mix(h, tier(f.count));
  h = mix(h, tier(f.strong));
  h = mix(h, tier(f.hidden));
  h = mix(h, tier(f.open));
  for (uint8_t b : f.band) h = mix(h, tier(b));
  h = mix(h, f.channel_span < 5 ? 0 : f.channel_span < 10 ? 1 : 2);
  f.signature = h;
  return f;
}
RfSpecimen rf_specimen(const RfFingerprint &f) {
  RfSpecimen s;
  s.rf = f;
  s.segments = uint8_t(4 + tier(f.count));
  s.antennae = uint8_t(2 + (f.channel_span >= 5) + (f.channel_span >= 10));
  s.spines = uint8_t(2 + (f.hidden > 0) + (f.hidden > 3) + (f.hidden > 8));
  s.eyes = uint8_t(1 + (f.open > 0) + (f.open > 5));
  s.threads = uint8_t(2 + tier(f.strong));
  s.mantle = uint8_t(14 + tier(f.count) * 3 + (f.strong > f.weak ? 3 : 0));
  s.asymmetry = uint8_t(f.band[0] > f.band[2] ? 1 : f.band[2] > f.band[0] ? 2 : 0);
  s.sigils = uint8_t(3 + (f.signature & 3u));
  const char *genera[] = {"Aetheromorphus", "Noctilacerta", "Umbrafilaris",
                         "Vesperonychus", "Pseudophyta", "Cryptolamina"};
  const char *species[] = {"tenebris", "reticulata", "vigilans",
                           "silens", "multiplex", "obscura"};
  std::snprintf(s.name, sizeof s.name, "%s %s",
                genera[(f.signature >> 9) % 6], species[(f.signature >> 19) % 6]);
  const char *density[] = {"Sparse habitat", "Scattered voices", "Crowded bands",
                           "Dense murmuration", "Saturated ether"};
  const char *quality = f.hidden ? "; veiled spoor" : f.open ? "; exposed nerves" : "; sealed chorus";
  std::snprintf(s.note, sizeof s.note, "%s%s", density[tier(f.count)], quality);
  return s;
}
}  // namespace badge
