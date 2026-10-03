#include <atomic>
#include <vector>

#include "assetpack.hpp"
#include "check.hpp"
#include "crc32.hpp"
#include "renderer.hpp"
#include "sim_panel.hpp"
#include "text.hpp"

using namespace badge;

namespace {
void put32(std::vector<uint8_t> &b, size_t off, uint32_t v) {
  for (int i = 0; i < 4; ++i) b[off + i] = uint8_t(v >> (8 * i));
}
void seal(std::vector<uint8_t> &b) {
  put32(b, 48, crc32(b.data() + 56, 2));
  put32(b, 16, crc32(b.data() + 32, b.size() - 32));
  put32(b, 20, crc32(b.data(), 20));
}
std::vector<uint8_t> fixture(uint8_t format = kAssetFormatGray2) {
  // Python fixture: assetpack.build_pack([(1,2,4,2,bytes.fromhex('1be4'))]).
  std::vector<uint8_t> b(60, 0);
  put32(b, 0, kAssetMagic);
  b[4] = 1; b[6] = 32; b[8] = 1;
  put32(b, 12, uint32_t(b.size()));
  b[32] = 1; b[34] = format; b[36] = 4; b[38] = 2;
  put32(b, 40, 56); put32(b, 44, 2);
  b[56] = 0x1b; b[57] = 0xe4;
  seal(b);
  return b;
}
}

TEST(four_tone_asset_validation_and_target_refusal) {
  auto b = fixture();
  CHECK(asset_pack_validate(b.data(), b.size()).status ==
        (target::kFourTone ? AssetStatus::Ok : AssetStatus::UnsupportedFormat));
  Bitmap image;
  CHECK_EQ(asset_pack_image(b.data(), 1, &image), target::kFourTone);
  MonoBitmap mono;
  CHECK(!asset_pack_bitmap(b.data(), 1, &mono));
  if (target::kFourTone) {
    CHECK_EQ(image.format, kAssetFormatGray2);
    CHECK_EQ(image.stride, 1);
    b[36] = 3; seal(b);  // invalid nonzero padding
    CHECK(asset_pack_validate(b.data(), b.size()).status == AssetStatus::BadEntry);
    b = fixture(); b[38] = 3; seal(b);
    CHECK(asset_pack_validate(b.data(), b.size()).status == AssetStatus::BadEntry);
    b = fixture(); b[36] = 0; seal(b);
    CHECK(asset_pack_validate(b.data(), b.size()).status == AssetStatus::BadEntry);
    b = fixture(); b[56] ^= 1;  // correct container CRC, corrupt entry CRC
    put32(b, 16, crc32(b.data() + 32, b.size() - 32));
    put32(b, 20, crc32(b.data(), 20));
    CHECK(asset_pack_validate(b.data(), b.size()).status == AssetStatus::BadEntryCrc);
  }
  b = fixture(99);
  CHECK(asset_pack_validate(b.data(), b.size()).status == AssetStatus::UnsupportedFormat);
  b = fixture();
  CHECK(asset_pack_validate(b.data(), b.size() - 1).status == AssetStatus::BadSize);
  CHECK(!asset_pack_image(b.data(), 99, &image));
  CHECK(!image.valid());
}

TEST(four_tone_pixels_storage_clip_copy_and_gray_diff) {
  static Framebuffer a, b;
  a.reset_clip(); a.clear(Ink::White);
  const uint8_t bits[] = {0x1b};  // white, light, dark, black
  CHECK_EQ(a.blit_gray2(bits, 4, 1, 1, 0, 0), target::kFourTone);
  if (!target::kFourTone) {
    CHECK(a.equals(Framebuffer()));
    CHECK_EQ(Framebuffer::kBytes, 4736);
    return;
  }
  CHECK_EQ(a.get(0, 0), Ink::White);
  CHECK_EQ(a.get(1, 0), Ink::LightGray);
  CHECK_EQ(a.get(2, 0), Ink::DarkGray);
  CHECK_EQ(a.get(3, 0), Ink::Black);
  CHECK_EQ(a.data()[0], 0x2d);  // logical enum order differs from GRAY2
  b.copy_from(a); CHECK(b.equals(a)); CHECK_EQ(b.hash(), a.hash());
  b.set(1, 0, Ink::DarkGray);
  CHECK(!b.equals(a)); CHECK(b.hash() != a.hash());
  Rect d = a.diff_bounds(b);
  CHECK(d.x == 1 && d.y == 0 && d.w == 1 && d.h == 1);
  a.clear(Ink::White); a.set_clip({0, 0, 2, 1});
  a.blit_gray2(bits, 4, 1, 1, -1, 0);
  CHECK_EQ(a.get(0, 0), Ink::LightGray); CHECK_EQ(a.get(1, 0), Ink::DarkGray);
  CHECK_EQ(a.get(2, 0), Ink::White); a.reset_clip();
  a.blit_gray2(bits, 4, 1, 1, Framebuffer::kWidth - 2, Framebuffer::kHeight - 1);
  CHECK_EQ(a.get(Framebuffer::kWidth - 1, Framebuffer::kHeight - 1), Ink::LightGray);
  a.set(-1, -1, Ink::DarkGray); a.set(Framebuffer::kWidth, 0, Ink::DarkGray);
}

TEST(four_tone_portrait_opt_in_both_layouts_ui_and_qr_stay_mono) {
  if (!target::kFourTone) return;
  static Settings s; settings_defaults(&s);
  RenderContext ctx; ctx.settings = &s;
  std::vector<uint8_t> bits(size_t(target::kPortraitWidth / 4) * target::kPortraitHeight, 0x1b);
  ctx.portrait.bits = bits.data(); ctx.portrait.width = target::kPortraitWidth;
  ctx.portrait.height = target::kPortraitHeight; ctx.portrait.stride = target::kPortraitWidth / 4;
  ctx.portrait.format = kAssetFormatGray2;
  ctx.status.battery.display = PowerDisplay::Usb;
  ctx.status.gesture = GestureIndicator::On;
  static Framebuffer fb;
  View v{}; v.screen = Screen::Badge;
  for (uint8_t layout : {0, 1}) {
    v.layout = layout; render(fb, v, ctx);
    const int x0 = layout ? Framebuffer::kWidth - target::kPortraitWidth : 0;
    int gray = 0;
    for (int y = 0; y < Framebuffer::kHeight; ++y)
      for (int x = 0; x < Framebuffer::kWidth; ++x) {
        const Ink ink = fb.get(x, y);
        if (ink == Ink::LightGray || ink == Ink::DarkGray) {
          CHECK(x >= x0 && x < x0 + target::kPortraitWidth); ++gray;
        }
      }
    CHECK(gray > 0);
  }
  for (Screen screen : {Screen::Card, Screen::QrFull, Screen::Projects, Screen::ProjectQr,
                        Screen::Index, Screen::Info, Screen::Recovery}) {
    v.screen = screen; render(fb, v, ctx);
    for (int y = 0; y < Framebuffer::kHeight; ++y)
      for (int x = 0; x < Framebuffer::kWidth; ++x)
        CHECK(fb.get(x, y) == Ink::White || fb.get(x, y) == Ink::Black);
  }
  // Explicit mono text overwrites gray with only black and white.
  fb.clear(Ink::LightGray);
  fb.fill_rect({0, 0, 100, 30}, Ink::White);
  draw_text(fb, fonts::sans_bold_10, 0, 0, "Test", Ink::Black);
  for (int y = 0; y < 30; ++y)
    for (int x = 0; x < 100; ++x) CHECK(fb.get(x, y) == Ink::White || fb.get(x, y) == Ink::Black);
  fb.clear(Ink::DarkGray);
  QrSymbol q; CHECK(qr_encode("https://example.com/alex", 128, &q) == QrStatus::Ok);
  qr_draw(fb, q, 3, 5);
  for (int y = 5; y < 5 + q.px; ++y)
    for (int x = 3; x < 3 + q.px; ++x) CHECK(fb.get(x, y) == Ink::White || fb.get(x, y) == Ink::Black);
}

TEST(four_tone_pipeline_keeps_gray_and_queued_changes) {
  if (!target::kFourTone) return;
  std::atomic<uint32_t> clock{0}; SimPanel panel(&clock); panel.partial = false;
  static Framebuffer bufs[2]; JobQueue jobs; EventQueue events;
  DisplayService display(panel, bufs, 2, jobs, events);
  RenderScheduler sched(bufs, 2, jobs, events);
  Ink scene = Ink::LightGray;
  auto draw = [](Framebuffer &fb, void *ctx) { fb.clear(Ink::White); fb.set(7, 13, *static_cast<Ink *>(ctx)); };
  auto step = [&]() { clock += 10; sched.poll(draw, &scene, 1, 0, clock); display.poll(clock);
                     sched.poll(draw, &scene, 1, 0, clock); };
  auto settle = [&]() { for (int i = 0; i < 1000 && !sched.settled(); ++i) step(); CHECK(sched.settled()); };
  display.start(1); sched.invalidate(); step();
  scene = Ink::DarkGray; sched.invalidate(); step(); settle();
  CHECK_EQ(panel.image.get(7, 13), Ink::DarkGray);
  CHECK_EQ(display.shown().get(7, 13), Ink::DarkGray);
  CHECK_EQ(display.stats().full, 2);
  sched.invalidate(); settle(); CHECK_EQ(sched.suppressed(), 1);
  CHECK_EQ(panel.violations, 0);
}
