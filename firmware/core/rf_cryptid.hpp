// RF Cryptid: portable, deterministic specimen model. No radio or SDK dependency.
// Raw identifiers are used only for scan deduplication; never rendered or stored.
#pragma once
#include <cstddef>
#include <cstdint>
namespace badge {
constexpr size_t kRfMaxAps = 48;
struct RfObservation {
  uint8_t bssid[6] = {};
  int16_t rssi = -100;
  uint8_t channel = 0;
  bool hidden = false;
  bool secured = true;
};
struct RfFingerprint {
  uint8_t count = 0, strong = 0, medium = 0, weak = 0;
  uint8_t hidden = 0, open = 0, band[3] = {};
  uint8_t channel_span = 0;
  uint32_t signature = 0;
};
struct RfSpecimen {
  RfFingerprint rf;
  uint8_t segments = 0, antennae = 0, spines = 0, eyes = 0, threads = 0;
  uint8_t mantle = 0, asymmetry = 0, sigils = 0;
  char name[48] = {};
  char note[48] = {};
};
enum class RfPlateState : uint8_t { Idle, Scanning, Ready, Empty, Error };
struct RfPlate {
  RfPlateState state = RfPlateState::Idle;
  RfSpecimen specimen;
  const char *message = nullptr;  // static literal, not stored in flash
};
// Order-independent, BSSID-deduplicated, bounded, coarsely quantized.
// No retained SSID, MAC, history, or precise RF listing in the result.
RfFingerprint rf_fingerprint(const RfObservation *rows, size_t n);
RfSpecimen rf_specimen(const RfFingerprint &rf);
}  // namespace badge
