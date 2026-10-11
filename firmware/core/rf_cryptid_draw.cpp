#include "rf_cryptid_draw.hpp"
#include <cmath>
#include <cstdio>
#include "text.hpp"
namespace badge {
namespace {
constexpr double kPi = 3.14159265358979323846;
void stroke(Framebuffer &fb, int x0, int y0, int x1, int y1) {
  int dx = x1 > x0 ? x1 - x0 : x0 - x1;
  int dy = y1 > y0 ? y1 - y0 : y0 - y1;
  int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
  int err = dx - dy;
  for (;;) {
    fb.set(x0, y0, Ink::Black);
    if (x0 == x1 && y0 == y1) break;
    int twice = 2 * err;
    if (twice > -dy) { err -= dy; x0 += sx; }
    if (twice < dx) { err += dx; y0 += sy; }
  }
}
void oval(Framebuffer &fb, int cx, int cy, int rx, int ry, int steps = 48) {
  int px = cx + rx, py = cy;
  for (int i = 1; i <= steps; ++i) {
    double a = (2.0 * kPi * i) / steps;
    int x = cx + int(std::lround(rx * std::cos(a)));
    int y = cy + int(std::lround(ry * std::sin(a)));
    stroke(fb, px, py, x, y);
    px = x; py = y;
  }
}
void star(Framebuffer &fb, int x, int y, int s) {
  stroke(fb, x-s, y, x+s, y); stroke(fb, x, y-s, x, y+s);
  fb.set(x-s-2, y-1, Ink::Black);
}
void glyph(Framebuffer &fb, int x, int y, int n) {
  oval(fb, x, y, 3+n%3, 5, 16);
  stroke(fb, x-8, y, x+8, y);
  stroke(fb, x, y-9, x+(n%3-1)*4, y-15);
  if (n&1) star(fb, x+10, y-9, 2);
}
void creature(Framebuffer &fb, const RfSpecimen &s) {
  const int cx = Framebuffer::kWidth / 2, cy = Framebuffer::kHeight / 2 + 1;
  const int w = s.mantle, h = Framebuffer::kHeight > 150 ? 36 : 27;
  // Central translucent mantle with a nested ink outline.
  oval(fb,cx,cy,w,h);
  oval(fb,cx,cy,w-5,h-6);
  oval(fb,cx,cy,w-10,h-13);
  // Dorsal rachis, enclosed ribs and fine veinlets.
  stroke(fb,cx,cy-h+6,cx,cy+h-7);
  for(int i=0;i<s.segments;i++){
    const int y=cy-h+12 + i*(2*h-24)/(s.segments-1);
    const int tapered=(y-cy)*(y-cy)*w/(h*h);
    const int reach=w-5-tapered;
    if(reach<5) continue;
    stroke(fb,cx,y-2,cx-reach,y+5);
    stroke(fb,cx,y-2,cx+reach,y+5);
    stroke(fb,cx-reach,y+5,cx-reach+6,y+9);
    stroke(fb,cx+reach,y+5,cx+reach-6,y+9);
    if(i%2==0) { stroke(fb,cx-3,y+1,cx-7,y+7); stroke(fb,cx+3,y+1,cx+7,y+7); }
  }
  // Radio branches: dominant channels skew the distal fronds, not the body.
  for(int sign=-1;sign<=1;sign+=2){
    for(int i=0;i<s.threads;i++){
      const int y=cy-27+i*15;
      const int x=cx+sign*(w-3);
      int spread=15+(i%2)*6+(sign>0 && s.asymmetry==2 ? 6:0);
      if(sign<0 && s.asymmetry==1) spread+=6;
      stroke(fb,x,y,x+sign*spread,y-12+(i%3)*10);
      stroke(fb,x+sign*spread,y-12+(i%3)*10,x+sign*(spread+9),y-21+(i%3)*10);
      star(fb,x+sign*(spread+9),y-21+(i%3)*10,2);
    }
    // Hidden networks produce barbed lateral spines.
    for(int i=0;i<s.spines;i++){
      int y=cy-28+i*21;
      int x=cx+sign*w;
      stroke(fb,x,y,x+sign*12,y-9);
      stroke(fb,x+sign*12,y-9,x+sign*8,y-16);
    }
  }
  // Antennae: channel spread controls the number of forked tendrils.
  for(int sign=-1;sign<=1;sign+=2) {
    for(int i=0;i<s.antennae;i++){
      const int base=cx+sign*(4+i*5);
      stroke(fb,base,cy-h+1,base+sign*(5+i*4),cy-h-15-i*4);
      stroke(fb,base+sign*(5+i*4),cy-h-15-i*4,base+sign*(7+i*4),cy-h-19-i*4);
      oval(fb,base+sign*(7+i*4),cy-h-21-i*4,2,3,16);
    }
  }
  // The ocular organs form a close cluster, not cartoon eyes.
  for(int i=0;i<s.eyes;i++){
    const int x=cx+(i-1)*9;
    oval(fb,x,cy-h+21,4,6,20);
    oval(fb,x,cy-h+21,1,2,12);
  }
  // Root tendrils vanish into the lower field-paper margin.
  for(int i=-2;i<=2;i++){
    const int x=cx+i*6;
    stroke(fb,x,cy+h-2,x+i*4,cy+h+12-(i*i));
    stroke(fb,x+i*4,cy+h+12-(i*i),x+i*7,cy+h+16-(i*i));
  }
}
} // namespace
void render_rf_plate(Framebuffer &fb, const RfPlate *plate) {
  const int W=Framebuffer::kWidth, H=Framebuffer::kHeight;
  fb.hline(8,23,W-16,Ink::Black);
  fb.hline(8,H-31,W-16,Ink::Black);
  draw_text(fb,fonts::sans_bold_12,9,4,"RF CRYPTID");
  draw_text(fb,fonts::sans_10,104,6,"TAB. I / AETHER");
  if(!plate || plate->state==RfPlateState::Idle || plate->state==RfPlateState::Scanning){
    draw_text(fb,fonts::sans_bold_14,45,H/2-10,"LISTENING TO THE ETHER");
    draw_text(fb,fonts::sans_10,55,H/2+14,"The habitat is not yet known.");
  }else if(plate->state!=RfPlateState::Ready){
    draw_text(fb,fonts::sans_bold_14,48,H/2-10,"NO SPECIMEN OBSERVED");
    const char *detail=plate->state==RfPlateState::Empty
      ? "The spectrum remains silent."
      : plate->message && plate->message[0] ? plate->message : "The survey was interrupted.";
    const Font *const small[]={&fonts::sans_10};
    const FitResult fitted=fit_text(small,1,detail,W-32);
    draw_fitted(fb,fitted,detail,16,H/2+14,W-32,Align::Center);
  }else{
    creature(fb,plate->specimen);
    for(int i=0;i<plate->specimen.sigils;i++){
      const int x=i%2?W-22:22, y=40+i*17;
      glyph(fb,x,y,int((plate->specimen.rf.signature>>(i*4))&7));
    }
    draw_text(fb,fonts::sans_bold_12,9,H-28,plate->specimen.name);
    draw_text(fb,fonts::sans_10,9,H-14,plate->specimen.note);
  }
}
} // namespace badge
