// Compile the real radio adapter with a host-only stub of the CYW43 poll API.
#include "rf_wifi.hpp"
#include "check.hpp"
#include <cstring>

namespace {
struct MockRadio {
  int init_calls=0, deinit_calls=0, enable_calls=0, disable_calls=0, poll_calls=0;
  int init_return=0, scan_return=0, scan_calls=0;
  int passive_flag=-1;
  bool active=false;
  void *callback_env=nullptr;
  int (*callback)(void *, const cyw43_ev_scan_result_t *)=nullptr;
} radio;
void reset_radio() { radio=MockRadio{}; }
}  // namespace

cyw43_t cyw43_state;
int cyw43_arch_init() { ++radio.init_calls; return radio.init_return; }
void cyw43_arch_deinit() { ++radio.deinit_calls; radio.active=false; }
void cyw43_arch_enable_sta_mode() { ++radio.enable_calls; }
void cyw43_arch_disable_sta_mode() { ++radio.disable_calls; }
void cyw43_arch_poll() { ++radio.poll_calls; }
int cyw43_wifi_scan(cyw43_t *, const cyw43_wifi_scan_options_t *options,
                    void *env, int (*callback)(void *, const cyw43_ev_scan_result_t *)) {
  ++radio.scan_calls;
  radio.passive_flag=options->scan_type;
  radio.callback_env=env;
  radio.callback=callback;
  if(!radio.scan_return) radio.active=true;
  return radio.scan_return;
}
bool cyw43_wifi_scan_active(cyw43_t *) { return radio.active; }

using namespace badge;

TEST(rf_wifi_timeout_shuts_down_even_when_driver_stays_scan_active) {
  reset_radio();
  RfWifiScanner scanner;
  scanner.request(1000);
  CHECK(scanner.plate().state==RfPlateState::Scanning);
  CHECK_EQ(radio.passive_flag,1);
  CHECK_EQ(radio.init_calls,1);
  CHECK_EQ(radio.enable_calls,1);
  CHECK(!scanner.poll(18999,true));  // 17,999 ms elapsed
  CHECK_EQ(radio.deinit_calls,0);
  // Driver still reports active, as it may after a nonzero escan status.
  CHECK(radio.active);
  CHECK(scanner.poll(19000,true));
  CHECK(scanner.plate().state==RfPlateState::Error);
  CHECK_STR(scanner.plate().message,"Survey timed out");
  CHECK_EQ(radio.disable_calls,1);
  CHECK_EQ(radio.deinit_calls,1);
  CHECK(!radio.active);
  CHECK(!scanner.poll(19001,true));
  CHECK_EQ(radio.deinit_calls,1);
  // A later request must initialize a clean new radio session.
  scanner.request(22000);
  CHECK_EQ(radio.init_calls,2);
  CHECK(scanner.plate().state==RfPlateState::Scanning);
  radio.active=false;
  CHECK(scanner.poll(23000,true));
  CHECK(scanner.plate().state==RfPlateState::Empty);
  CHECK_EQ(radio.deinit_calls,2);
}
TEST(rf_wifi_timeout_powers_down_even_when_screen_is_hidden) {
  reset_radio();
  RfWifiScanner scanner;
  scanner.request(0);
  CHECK(!scanner.poll(18000,false));
  CHECK(scanner.plate().state==RfPlateState::Error);
  CHECK_EQ(radio.deinit_calls,1);
}
TEST(rf_wifi_scan_completion_and_failure_cleanup) {
  reset_radio();
  RfWifiScanner scanner;
  scanner.request(500);
  cyw43_ev_scan_result_t seen;
  seen.bssid[5]=12; seen.channel=6; seen.rssi=-54;
  seen.ssid_len=4; seen.auth_mode=1;
  CHECK(radio.callback!=nullptr);
  CHECK_EQ(radio.callback(radio.callback_env,&seen),0);
  radio.active=false;
  CHECK(scanner.poll(900,true));
  CHECK(scanner.plate().state==RfPlateState::Ready);
  CHECK_EQ(scanner.plate().specimen.rf.count,1);
  CHECK_EQ(radio.deinit_calls,1);

  reset_radio();
  RfWifiScanner rejected;
  radio.scan_return=-1;
  rejected.request(0);
  CHECK(rejected.plate().state==RfPlateState::Error);
  CHECK_STR(rejected.plate().message,"Survey rejected by radio");
  CHECK(!rejected.poll(100,true));
  CHECK_EQ(radio.deinit_calls,1);

  reset_radio();
  RfWifiScanner no_radio;
  radio.init_return=-1;
  no_radio.request(0);
  CHECK(no_radio.plate().state==RfPlateState::Error);
  CHECK_STR(no_radio.plate().message,"Radio initialization failed");
  CHECK_EQ(radio.deinit_calls,0);
}
