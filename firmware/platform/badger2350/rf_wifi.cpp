#include "rf_wifi.hpp"
#include <cstring>
namespace badge {
namespace {
constexpr uint32_t kRfScanDeadlineMs = 18000;  // bounded, even if driver stops responding
}
void RfWifiScanner::stop() {
  if(!initialized_) return;
  cyw43_arch_disable_sta_mode();
  cyw43_arch_deinit();
  initialized_=false;
  scanning_=false;
}
int RfWifiScanner::on_result(void *env, const cyw43_ev_scan_result_t *result) {
  if(result) static_cast<RfWifiScanner*>(env)->receive(*result);
  return 0;
}
void RfWifiScanner::receive(const cyw43_ev_scan_result_t &r) {
  if(!scanning_ || r.channel < 1 || r.channel > 14) return;
  RfObservation a;
  std::memcpy(a.bssid,r.bssid,6);
  a.rssi=r.rssi;
  a.channel=uint8_t(r.channel);
  a.hidden=r.ssid_len==0;
  a.secured=r.auth_mode!=CYW43_AUTH_OPEN;
  // One BSSID per plate. Callback ordering must not determine the sample.
  for(size_t i=0;i<count_;++i)
    if(std::memcmp(rows_[i].bssid,a.bssid,6)==0){
      if(a.rssi>rows_[i].rssi || (a.rssi==rows_[i].rssi && a.channel<rows_[i].channel))
        rows_[i]=a;
      return;
    }
  if(count_<kRfMaxAps){rows_[count_++]=a;return;}
  // On busy venues keep the strongest 48, with deterministic BSSID ties.
  size_t weakest=0;
  for(size_t i=1;i<count_;++i)
    if(rows_[i].rssi<rows_[weakest].rssi ||
       (rows_[i].rssi==rows_[weakest].rssi &&
        std::memcmp(rows_[i].bssid,rows_[weakest].bssid,6)>0)) weakest=i;
  if(a.rssi>rows_[weakest].rssi ||
     (a.rssi==rows_[weakest].rssi &&
      std::memcmp(a.bssid,rows_[weakest].bssid,6)<0)) rows_[weakest]=a;
}
void RfWifiScanner::request(uint32_t now_ms) {
  if(scanning_) return;  // do not queue overlapping surveys
  plate_.state=RfPlateState::Scanning;
  plate_.message=nullptr;
  if(!initialized_) {
    if(cyw43_arch_init()!=0) {
      plate_.state=RfPlateState::Error;
      plate_.message="Radio initialization failed";
      return;
    }
    initialized_=true;
    cyw43_arch_enable_sta_mode();
  }
  count_=0;
  started_=now_ms;
  cyw43_wifi_scan_options_t opts = {};
  opts.scan_type=1; // passive; do not send active probe requests
  if(cyw43_wifi_scan(&cyw43_state,&opts,this,&RfWifiScanner::on_result)!=0) {
    plate_.state=RfPlateState::Error;
    plate_.message="Survey rejected by radio";
    return;
  }
  scanning_=true;
}
bool RfWifiScanner::poll(uint32_t now_ms, bool visible) {
  if(!initialized_)return false;
  cyw43_arch_poll();
  bool redraw=false;
  if(scanning_ && !cyw43_wifi_scan_active(&cyw43_state)){
    scanning_=false;
    const RfFingerprint f=rf_fingerprint(rows_,count_);
    plate_.specimen=rf_specimen(f);
    plate_.state=f.count?RfPlateState::Ready:RfPlateState::Empty;
    redraw=true;
  }else if(scanning_ && now_ms-started_>=kRfScanDeadlineMs){
    scanning_=false;
    plate_.state=RfPlateState::Error;
    plate_.message="Survey timed out";
    redraw=true;
  }
  // The radio is off between visits, including after an interrupted survey.
  // If the driver is still busy on timeout, continue polling until inactive.
  if(!scanning_ && !cyw43_wifi_scan_active(&cyw43_state)) stop();
  return redraw && visible;
}
} // namespace badge
