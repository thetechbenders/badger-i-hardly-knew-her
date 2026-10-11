// Test-only stand-in for the Pico CYW43 arch. Never used in firmware.
#pragma once
#include <cstdint>

struct cyw43_t {};
extern cyw43_t cyw43_state;
struct cyw43_ev_scan_result_t {
  uint8_t bssid[6] = {};
  int16_t rssi = 0;
  uint8_t channel = 0;
  uint8_t ssid_len = 0;
  uint32_t auth_mode = 0;
};
struct cyw43_wifi_scan_options_t {
  uint8_t scan_type = 0;
};
constexpr uint32_t CYW43_AUTH_OPEN = 0;
int cyw43_arch_init();
void cyw43_arch_deinit();
void cyw43_arch_enable_sta_mode();
void cyw43_arch_disable_sta_mode();
void cyw43_arch_poll();
int cyw43_wifi_scan(cyw43_t *state, const cyw43_wifi_scan_options_t *options,
                    void *env, int (*callback)(void *, const cyw43_ev_scan_result_t *));
bool cyw43_wifi_scan_active(cyw43_t *state);
