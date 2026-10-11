#include <cstring>
#include "app.hpp"
#include "check.hpp"
#include "rf_cryptid.hpp"
#include "rf_cryptid_draw.hpp"
#include "renderer.hpp"
using namespace badge;
namespace {
RfObservation ap(uint8_t id,int16_t dbm,uint8_t ch,bool hidden=false,bool open=false){
  RfObservation x;
  x.bssid[5]=id; x.rssi=dbm; x.channel=ch;
  x.hidden=hidden; x.secured=!open; return x;
}
}
TEST(rf_normalization_deduplicates_and_ignores_order) {
  const RfObservation a[]={ap(1,-50,1),ap(2,-69,6,true),ap(3,-82,11,false,true),
                            ap(1,-56,1),ap(4,-93,0)};
  const RfObservation b[]={a[2],a[3],a[1],a[4],a[0]};
  const auto x=rf_fingerprint(a,5), y=rf_fingerprint(b,5);
  CHECK_EQ(x.count,3);
  CHECK_EQ(x.strong,1);
  CHECK_EQ(x.medium,1);
  CHECK_EQ(x.weak,1);
  CHECK_EQ(x.hidden,1);
  CHECK_EQ(x.open,1);
  CHECK_EQ(x.channel_span,10);
  CHECK_EQ(x.signature,y.signature);
  CHECK_EQ(std::strcmp(rf_specimen(x).name,rf_specimen(y).name),0);
  CHECK_EQ(rf_specimen(x).segments,rf_specimen(y).segments);
}
TEST(rf_jitter_keeps_morphology_and_ecological_name) {
  const RfObservation a[]={ap(1,-50,1),ap(2,-67,6),ap(3,-87,11)};
  const RfObservation b[]={ap(1,-52,1),ap(2,-69,6),ap(3,-89,11)};
  const auto x=rf_specimen(rf_fingerprint(a,3));
  const auto y=rf_specimen(rf_fingerprint(b,3));
  CHECK_EQ(x.rf.signature,y.rf.signature);
  CHECK_EQ(std::strcmp(x.name,y.name),0);
  CHECK_EQ(x.segments,y.segments);
  CHECK_EQ(x.mantle,y.mantle);
}
TEST(rf_distinct_ecologies_produce_distinct_anatomy) {
  RfObservation crowded[24]={};
  for(int i=0;i<24;++i)
    crowded[i]=ap(uint8_t(i+1),-52,uint8_t(1+i%13),i%3==0,i%4==0);
  const auto small=rf_specimen(rf_fingerprint(nullptr,0));
  const auto big=rf_specimen(rf_fingerprint(crowded,24));
  CHECK_EQ(small.rf.count,0);
  CHECK_EQ(big.rf.count,24);
  CHECK(big.segments>small.segments);
  CHECK(big.threads>small.threads);
  CHECK(big.spines>small.spines);
  CHECK(big.eyes>small.eyes);
  CHECK(big.rf.signature!=small.rf.signature);
}
TEST(rf_plate_host_render_is_reproducible_and_stateful) {
  Settings settings;
  settings_defaults(&settings);
  RfObservation rows[]={ap(1,-54,1),ap(2,-73,6,true),ap(3,-80,11),ap(4,-64,13,false,true)};
  RfPlate plate;
  plate.state=RfPlateState::Ready;
  plate.specimen=rf_specimen(rf_fingerprint(rows,4));
  RenderContext ctx;
  ctx.settings=&settings;
  ctx.rf=&plate;
  View v;
  v.screen=Screen::Cryptid;
  Framebuffer one,two;
  render(one,v,ctx); render(two,v,ctx);
  CHECK(one.equals(two));
  const uint32_t specimen_hash=one.hash();
  plate.state=RfPlateState::Scanning;
  render(one,v,ctx);
  CHECK(one.hash()!=specimen_hash);
  plate.state=RfPlateState::Empty;
  render(one,v,ctx);
  CHECK(one.hash()!=specimen_hash);
  CHECK(std::strcmp(screen_name(Screen::Cryptid),"cryptid")==0);
}
TEST(rf_access_requires_capability_and_not_safe_mode) {
  App a; AppConfig c;
  a.boot(c,-1,0);
  CHECK_EQ(a.on_screen_request(Screen::Cryptid),kActNone);
  CHECK(a.view().screen==Screen::Badge);
  c.rf_cryptid=true;
  a.boot(c,-1,0);
  a.on_screen_request(Screen::Info);
  a.on_button({Button::C,Gesture::Press,1000});
  const uint32_t action=a.on_button({Button::C,Gesture::Long,1400});
  CHECK((action & kActRfScan)!=0);
  CHECK(a.view().screen==Screen::Cryptid);
  CHECK_EQ(a.on_screen_request(Screen::Cryptid),kActRfScan | kActRedraw);
  a.on_button({Button::C,Gesture::Press,1750});
  a.on_button({Button::C,Gesture::Short,1800});
  const uint32_t rescan=a.on_poll(1800+kDoublePressMs+1);
  CHECK((rescan & kActRfScan)!=0);
  CHECK((rescan & kActRedraw)!=0);
  CHECK((a.on_button({Button::A,Gesture::Short,2200})&kActRedraw)!=0);
  CHECK(a.view().screen==Screen::Badge);
  c.safe_mode=true;
  a.boot(c,-1,0);
  CHECK_EQ(a.on_screen_request(Screen::Cryptid),kActNone);
}
