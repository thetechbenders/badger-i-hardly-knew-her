#include "board.hpp"

#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "pico/stdlib.h"

namespace badge::board {

namespace {
constexpr uint kPinsButton[kButtonCount] = {BADGER2040_SW_A_PIN, BADGER2040_SW_B_PIN, BADGER2040_SW_C_PIN,
                                           BADGER2040_SW_UP_PIN, BADGER2040_SW_DOWN_PIN,
                                           BADGER2040_USER_SW_PIN};
uint32_t g_wake = 0;

uint32_t gpio_to_buttons(uint32_t gpio, bool include_user) {
  uint32_t m = 0;
  for (int i = 0; i < kButtonCount; ++i) {
    if (Button(i) == Button::User) {
      if (include_user && !(gpio & (1u << kPinsButton[i]))) m |= 1u << i;  // active low
    } else if (gpio & (1u << kPinsButton[i])) {
      m |= 1u << i;  // active high, external press pulls the pin up
    }
  }
  return m;
}
}  // namespace

// Runs before C++ static constructors of lower priority and before main():
// latch power as early as possible (a short button tap must be enough to
// wake), then capture which button caused the wake. Mirrors Pimoroni's
// wakeup module (badger2040/firmware/modules/wakeup).
extern "C" void __attribute__((constructor(101))) badger_early_wake() {
  gpio_init(BADGER2040_3V3_EN_PIN);
  gpio_set_dir(BADGER2040_3V3_EN_PIN, GPIO_OUT);
  gpio_put(BADGER2040_3V3_EN_PIN, 1);
  uint32_t state = gpio_get_all();
  state |= gpio_get_all();
  g_wake = gpio_to_buttons(state, false);
}

uint32_t wake_buttons() { return g_wake; }

int wake_button() {
  for (int i = 0; i < int(Button::User); ++i)
    if (g_wake & (1u << i)) return i;
  return -1;
}

void init() {
  hold_power_latch();
  for (int i = 0; i < kButtonCount; ++i) {
    const uint pin = kPinsButton[i];
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    if (Button(i) == Button::User) gpio_pull_up(pin); else gpio_pull_down(pin);
  }
  gpio_init(BADGER2040_VBUS_DETECT_PIN);
  gpio_set_dir(BADGER2040_VBUS_DETECT_PIN, GPIO_IN);
  gpio_disable_pulls(BADGER2040_VBUS_DETECT_PIN);

  gpio_init(BADGER2040_VREF_POWER_PIN);
  gpio_set_dir(BADGER2040_VREF_POWER_PIN, GPIO_OUT);
  gpio_put(BADGER2040_VREF_POWER_PIN, 0);
  adc_init();
  adc_gpio_init(BADGER2040_1V2_REF_PIN);
  adc_gpio_init(BADGER2040_BAT_SENSE_PIN);

  gpio_set_function(BADGER2040_USER_LED_PIN, GPIO_FUNC_PWM);
  const uint slice = pwm_gpio_to_slice_num(BADGER2040_USER_LED_PIN);
  pwm_config cfg = pwm_get_default_config();
  pwm_config_set_wrap(&cfg, 255 * 255);
  pwm_init(slice, &cfg, true);
  led(0);
}

uint32_t read_buttons() { return gpio_to_buttons(gpio_get_all(), true); }

bool usb_powered() { return gpio_get(BADGER2040_VBUS_DETECT_PIN); }

BatteryRaw read_battery_raw() {
  // Circuit (Pico SDK board header + Pimoroni's Badger 2040 battery example):
  // GPIO27 powers a 1.24 V reference on GPIO28 (ADC2); GPIO29 (ADC3) senses
  // the battery through a divider with gain 1/3. The RP2040 ADC is referenced
  // to its 3V3 supply, which sags on a low cell, so the reference reading is
  // used to recover the true supply voltage.
  gpio_put(BADGER2040_VREF_POWER_PIN, 1);
  sleep_us(1000);  // reference settling
  auto read = [](uint input) {
    adc_select_input(input);
    (void)adc_read();  // discard the first conversion after switching input
    uint32_t sum = 0;
    for (int i = 0; i < 32; ++i) sum += adc_read();
    return uint16_t((sum + 16) / 32);
  };
  BatteryRaw r;
  r.ref_counts = read(BADGER2040_1V2_REF_PIN - 26);  // ADC2
  r.bat_counts = read(BADGER2040_BAT_SENSE_PIN - 26);  // ADC3
  gpio_put(BADGER2040_VREF_POWER_PIN, 0);
  return r;
}

void led(uint8_t level) {
  // Gamma-ish curve: perceived brightness roughly linear in `level`.
  pwm_set_gpio_level(BADGER2040_USER_LED_PIN, uint16_t(level) * level);
}

void hold_power_latch() {
  gpio_init(BADGER2040_3V3_EN_PIN);
  gpio_set_dir(BADGER2040_3V3_EN_PIN, GPIO_OUT);
  gpio_put(BADGER2040_3V3_EN_PIN, 1);
}

void release_power_latch() { gpio_put(BADGER2040_3V3_EN_PIN, 0); }

}  // namespace badge::board
