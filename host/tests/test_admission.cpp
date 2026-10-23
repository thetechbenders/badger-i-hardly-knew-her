#include <cstdlib>
#include <string>
#include "admission.hpp"
#include "app.hpp"
#include "check.hpp"
#include "qr.hpp"
using namespace badge;
namespace {
constexpr bool supported = Framebuffer::kHeight == 176;
const char payload[] = "BEGIN:VCARD\r\nVERSION:3.0\r\nN:Example;Fictional;;;\r\nFN:Fictional Example\r\n"
    "ORG:Imaginary Exhibition 2026\r\nNOTE:TEST ONLY - NOT VALID FOR ADMISSION. Reference FAKE-00000000\r\n"
    "URL:https://example.invalid/ticket/fictional-reference\r\nEND:VCARD\r\n";
App opened(uint32_t time = 100) {
  App a;
  AppConfig c;
  c.admission_configured = true;
  c.gesture_default_on = true;
  a.boot(c, -1, 0);
  a.on_screen_request(Screen::Info);
  a.on_button({Button::A, Gesture::Long, time});
  a.on_button({Button::B, Gesture::Press, time + 50});
  a.on_button({Button::B, Gesture::Long, time + 1050});
  return a;
}
}
TEST(admission_disabled_and_target_guard) {
  CHECK(kPrivateAdmission == nullptr);
  App a;
  a.boot({}, -1, 0);
  a.on_screen_request(Screen::Info);
  a.on_button({Button::A, Gesture::Long, 100});
  a.on_button({Button::B, Gesture::Long, 1200});
  CHECK(a.view().screen != Screen::Admission);
  a = opened();
  CHECK_EQ(a.view().screen == Screen::Admission, supported);
  Screen s;
  CHECK(!screen_from_name("private", &s));
  CHECK(!screen_from_name("admission", &s));
}
TEST(admission_deliberate_sequence_and_cancellation) {
  if (!supported) return;
  for (int incompatible = 0; incompatible < 7; ++incompatible) {
    App a;
    AppConfig c;
    c.admission_configured = true;
    c.safe_mode = incompatible == 6;
    a.boot(c, -1, 0);
    a.on_screen_request(Screen::Info);
    a.on_button({Button::A, Gesture::Long, 100});
    switch (incompatible) {
      case 0: a.on_poll(3100); break;
      case 1: a.on_button({Button::C, Gesture::Press, 200}); break;
      case 2: a.on_screen_request(Screen::Info); break;
      case 3: a.on_swipe(Swipe::Up, 200); break;
      case 4: a.on_activity(200); break;
      case 5: a.boot(c, -1, 200); a.on_screen_request(Screen::Info); break;
      default: break;
    }
    a.on_button({Button::B, Gesture::Long, incompatible == 0 ? 3200u : 1200u});
    CHECK(a.view().screen != Screen::Admission);
  }
  App a = opened(0xfffffc00u);  // sequence crosses clock wrap
  CHECK(a.view().screen == Screen::Admission);
  a.on_screen_request(Screen::Badge);
  a.on_swipe(Swipe::Down, 2000);
  CHECK(a.view().screen == Screen::Admission);
  CHECK_EQ(a.on_poll(15000), kActNone); // wrap: <15 seconds since reveal
  CHECK(a.on_poll(16000) & kActCleanRefresh);
  CHECK(a.concealing_admission());
}
TEST(admission_dismissal_timeout_sleep_and_modal_replacement) {
  if (!supported) return;
  App a = opened();
  CHECK_EQ(a.on_poll(16149), kActNone);
  CHECK(a.on_poll(16150) & kActCleanRefresh);
  CHECK(a.concealing_admission());
  CHECK(a.sleep_view().screen != Screen::Admission);
  a.on_button({Button::A, Gesture::Short, 17000});
  a.on_screen_request(Screen::Projects);
  a.on_project_step(1);
  a.on_swipe(Swipe::Down, 17000);
  CHECK(a.view().screen == Screen::Info);
  a.admission_concealed();
  a.on_button({Button::A, Gesture::Short, 18000});
  CHECK(a.view().screen == Screen::Badge);
  for (int b = 0; b < kButtonCount; ++b) {
    a = opened();
    CHECK(a.on_button({Button(b), Gesture::Press, 1200}) & kActCleanRefresh);
    CHECK(a.concealing_admission());
  }
  a = opened();
  const uint32_t actions = a.request_sleep();
  CHECK(actions & kActSleep);
  CHECK(actions & kActCleanRefresh);
  CHECK(a.concealing_admission());
  CHECK(a.sleep_view().screen != Screen::Admission);
}
TEST(admission_pixels) {
  Framebuffer fb;
  Admission ticket{payload, "TEST", "Example", 15};
  if (!supported) { CHECK(!render_admission(fb, &ticket)); return; }
  QrSymbol q;
  CHECK(qr_encode(payload, 176, &q, true) == QrStatus::Ok);
  CHECK(render_admission(fb, &ticket));
  const int offset = (176 - q.px) / 2;
  for (int y = 0; y < q.px; ++y)
    for (int x = 0; x < q.px; ++x) {
      const int mx = x / q.scale - 4, my = y / q.scale - 4;
      const bool black = mx >= 0 && my >= 0 && mx < q.modules && my < q.modules && q.module(mx, my);
      CHECK_EQ(fb.get(x + offset, y + offset) == Ink::Black, black);
    }
  CHECK_EQ(q.scale, 176 / (q.modules + 8));
  // Only this fictional fixture can be exported, never a private input.
  if (const char *path = std::getenv("BADGER_ADMISSION_TEST_IMAGE")) {
    FILE *f = std::fopen(path, "wb");
    CHECK(f != nullptr);
    if (f) {
      std::fprintf(f, "P4\n%d %d\n", fb.kWidth, fb.kHeight);
      std::fwrite(fb.data(), 1, fb.kBytes, f);
      std::fclose(f);
    }
  }
  const std::string maximum(520, 'x');
  CHECK(qr_encode(maximum.c_str(), 176, &q, true) == QrStatus::Ok);
  const std::string too_long(2000, 'x');
  ticket.payload = too_long.c_str();
  CHECK(!render_admission(fb, &ticket));
}

TEST(admission_cannot_rearm_during_sleep_and_idle_sleep_conceals) {
  if (!supported) return;
  App a;
  AppConfig c;
  c.admission_configured = true;
  c.sleep_timeout_s = 1;
  a.boot(c, -1, 0);
  a.on_screen_request(Screen::Info);
  a.on_button({Button::A, Gesture::Long, 100});
  a.on_button({Button::B, Gesture::Long, 1200});
  CHECK(a.view().screen == Screen::Admission);
  const uint32_t action = a.on_tick(2200, true, false);
  CHECK(action & kActSleep);
  CHECK(action & kActCleanRefresh);
  CHECK(a.concealing_admission());
  a.admission_concealed();
  a.on_button({Button::A, Gesture::Long, 2300});
  a.on_button({Button::B, Gesture::Long, 3400});
  CHECK(a.view().screen != Screen::Admission);
  CHECK(a.sleeping());
  a.on_screen_request(Screen::Admission);
  CHECK(a.view().screen != Screen::Admission);
}
