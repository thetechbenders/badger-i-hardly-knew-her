#include "board.hpp"

#include "diagnostics.hpp"
#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/powman.h"
#include "hardware/resets.h"
#include "hardware/structs/usb.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

namespace badge::board {

namespace {
constexpr uint kPinsButton[kButtonCount] = {BADGER2350_SW_A_PIN, BADGER2350_SW_B_PIN, BADGER2350_SW_C_PIN,
                                           BADGER2350_SW_UP_PIN, BADGER2350_SW_DOWN_PIN,
                                           BADGER2350_SW_HOME_PIN};
// powman wake channel for the button interrupt line (BadgeWare: PWRUP3).
constexpr uint kWakeChannel = 3;
uint32_t g_wake = 0;

void init_buttons() {
  for (uint pin : kPinsButton) {
    gpio_init(pin);  // SIO input, pad isolation cleared after a power-down
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_up(pin);
  }
}

uint32_t gpio_to_buttons(uint32_t gpio, bool include_home) {
  uint32_t m = 0;
  for (int i = 0; i < kButtonCount; ++i) {
    if (Button(i) == Button::User && !include_home) continue;
    if (!(gpio & (1u << kPinsButton[i]))) m |= 1u << i;  // active low
  }
  return m;
}
}  // namespace

// Runs before C++ static constructors of lower priority and before main():
// capture which button woke the chip while it is most likely still held.
// After a power-down the pads come back with their reset pulls (pull-down),
// so pull-ups go on first and get a moment to settle; then the buttons are
// read twice, 5 ms apart, and a button counts if either read saw it (as
// BadgeWare's latch_inputs()). A button wrongly counted is only ignored
// until it reads released, which it already does.
extern "C" void __attribute__((constructor(101))) badger_early_wake() {
  init_buttons();
  busy_wait_us(100);
  uint32_t pressed = gpio_to_buttons(gpio_get_all(), false);
  busy_wait_us(5000);
  pressed |= gpio_to_buttons(gpio_get_all(), false);
  g_wake = pressed;
}

uint32_t wake_buttons() { return g_wake; }

int wake_button() {
  for (int i = 0; i < int(Button::User); ++i)
    if (g_wake & (1u << i)) return i;
  return -1;
}

diag::ResetFlags reset_flags() {
  // CHIP_RESET.HAD_* describe the last chip-level reset; a wake from power-
  // down (HAD_SWCORE_PD) sets none of the flags below and classifies as a
  // cold start, which it is (SRAM was off). watchdog_caused_reboot() also
  // checks the boot ROM saw a normal boot (not BOOTSEL / a debugger load).
  const uint32_t chip = powman_hw->chip_reset;
  diag::ResetFlags f;
  f.watchdog = watchdog_caused_reboot();
  f.por = chip & (POWMAN_CHIP_RESET_HAD_POR_BITS | POWMAN_CHIP_RESET_HAD_BOR_BITS |
                  POWMAN_CHIP_RESET_HAD_GLITCH_DETECT_BITS);
  f.run_pin = chip & POWMAN_CHIP_RESET_HAD_RUN_LOW_BITS;
  f.debugger = chip & (POWMAN_CHIP_RESET_HAD_DP_RESET_REQ_BITS | POWMAN_CHIP_RESET_HAD_HZD_SYS_RESET_REQ_BITS |
                       POWMAN_CHIP_RESET_HAD_RESCUE_BITS);
  return f;
}

void init() {
  // PSRAM is not used (or initialised): keep its chip select, on the QSPI
  // bus shared with the flash, deselected. The board pulls it up as well.
  gpio_init(BADGER2350_PSRAM_CS_PIN);
  gpio_set_dir(BADGER2350_PSRAM_CS_PIN, GPIO_IN);
  gpio_pull_up(BADGER2350_PSRAM_CS_PIN);

  init_buttons();
  gpio_init(BADGER2350_SW_INT_PIN);
  gpio_set_dir(BADGER2350_SW_INT_PIN, GPIO_IN);
  gpio_pull_up(BADGER2350_SW_INT_PIN);
  gpio_init(BADGER2350_VBUS_DETECT_PIN);
  gpio_set_dir(BADGER2350_VBUS_DETECT_PIN, GPIO_IN);
  gpio_disable_pulls(BADGER2350_VBUS_DETECT_PIN);  // driven by the board, active high

  // Switched supply of the I2C bus (RTC and Qw/ST): on while awake, as in
  // BadgeWare. Turned off again (pin released) before power-down.
  gpio_init(BADGER2350_SW_POWER_EN_PIN);
  gpio_set_dir(BADGER2350_SW_POWER_EN_PIN, GPIO_OUT);
  gpio_put(BADGER2350_SW_POWER_EN_PIN, 1);

  adc_init();
  adc_gpio_init(BADGER2350_VBAT_SENSE_PIN);
  adc_gpio_init(BADGER2350_SENSE_1V1_PIN);
}

uint32_t read_buttons() { return gpio_to_buttons(gpio_get_all(), true); }

bool usb_powered() { return gpio_get(BADGER2350_VBUS_DETECT_PIN); }

BatteryRaw read_battery_raw() {
  // Same sampling as the Badger 2040 (32-sample average, first conversion
  // after switching inputs discarded); the reference needs no enable pin.
  auto read = [](uint input) {
    adc_select_input(input);
    (void)adc_read();
    uint32_t sum = 0;
    for (int i = 0; i < 32; ++i) sum += adc_read();
    return uint16_t((sum + 16) / 32);
  };
  BatteryRaw r;
  r.ref_counts = read(BADGER2350_SENSE_1V1_PIN - 26);  // ADC2
  r.bat_counts = read(BADGER2350_VBAT_SENSE_PIN - 26);  // ADC0
  return r;
}

void led(uint8_t) {}

namespace {
// The last steps run from RAM: the XIP domain powers down with the core.
void __no_inline_not_in_flash_func(enter_power_down)(powman_power_state off) {
  if (powman_set_power_state(off) != PICO_OK) return;
  while (true) __wfi();
}

void usb_phy_power_down() {
  // BadgeWare's sequence: reset the USB controller, take the PHY, power its
  // transmitter/receiver down and pull D+/D- low.
  reset_block_mask(RESETS_RESET_USBCTRL_BITS);
  unreset_block_mask_wait_blocking(RESETS_RESET_USBCTRL_BITS);
  usb_hw->muxing = USB_USB_MUXING_TO_PHY_BITS | USB_USB_MUXING_SOFTCON_BITS;
  usb_hw->phy_direct = USB_USBPHY_DIRECT_TX_PD_BITS | USB_USBPHY_DIRECT_RX_PD_BITS |
                       USB_USBPHY_DIRECT_DM_PULLDN_EN_BITS | USB_USBPHY_DIRECT_DP_PULLDN_EN_BITS;
  usb_hw->phy_direct_override =
      USB_USBPHY_DIRECT_OVERRIDE_TX_PD_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_RX_PD_OVERRIDE_EN_BITS |
      USB_USBPHY_DIRECT_OVERRIDE_DM_PULLDN_EN_OVERRIDE_EN_BITS |
      USB_USBPHY_DIRECT_OVERRIDE_DP_PULLDN_EN_OVERRIDE_EN_BITS;
}
}  // namespace

void power_off() {
  if (usb_powered()) return;  // emulated, as on the Badger 2040

  // The wake source is an edge on BUTTON_INT: wait (bounded, watchdog fed)
  // for the button that asked for power-off to be released, so the release
  // itself cannot count. Still held after that: the next press wakes.
  const absolute_time_t deadline = make_timeout_time_ms(3000);
  while (!gpio_get(BADGER2350_SW_INT_PIN) && !time_reached(deadline)) {
    watchdog_update();
    sleep_ms(10);
  }

  // Core 1 back into the boot ROM's wait loop (its state under BadgeWare),
  // then nothing else may run: interrupts stay off from here.
  multicore_reset_core1();
  (void)save_and_disable_interrupts();
  // Run from the USB PLL and stop the system PLL, as BadgeWare does first.
  set_sys_clock_48mhz();

  // Pads for the lowest drain (BadgeWare powman_init()): everything a
  // pulled-down input, except the buttons (pull-ups, needed for BUTTON_INT),
  // the PSRAM chip select (pull-up), and the reset-button sense, HOME,
  // the switched-supply enable (released: supply off) and both ADC inputs,
  // which float.
  for (uint pin = 0; pin < NUM_BANK0_GPIOS; ++pin) {
    gpio_set_function(pin, GPIO_FUNC_SIO);
    gpio_set_dir(pin, GPIO_IN);
    gpio_set_input_enabled(pin, false);
    switch (pin) {
      case BADGER2350_PSRAM_CS_PIN: gpio_set_pulls(pin, true, false); break;
      case BADGER2350_RESET_SW_PIN:
      case BADGER2350_SW_HOME_PIN:
      case BADGER2350_SW_POWER_EN_PIN:
      case BADGER2350_VBAT_SENSE_PIN:
      case BADGER2350_SENSE_1V1_PIN: gpio_disable_pulls(pin); break;
      case BADGER2350_SW_A_PIN:
      case BADGER2350_SW_B_PIN:
      case BADGER2350_SW_C_PIN:
      case BADGER2350_SW_UP_PIN:
      case BADGER2350_SW_DOWN_PIN: break;
      default: gpio_set_pulls(pin, false, true);
    }
  }
  init_buttons();
  usb_phy_power_down();

  hw_set_bits(&powman_hw->vreg_ctrl, POWMAN_PASSWORD_BITS | POWMAN_VREG_CTRL_UNLOCK_BITS);
  powman_timer_start();
  powman_set_debug_power_request_ignored(true);  // power down even with SWD attached

  // Wake on a falling edge of BUTTON_INT (any front button), into the normal
  // running state; boot vectors cleared so the chip boots from flash.
  gpio_init(BADGER2350_SW_INT_PIN);
  gpio_set_dir(BADGER2350_SW_INT_PIN, GPIO_IN);
  gpio_pull_up(BADGER2350_SW_INT_PIN);
  powman_enable_gpio_wakeup(kWakeChannel, BADGER2350_SW_INT_PIN, true, false);
  powman_power_state on = POWMAN_POWER_STATE_NONE;
  on = powman_power_state_with_domain_on(on, POWMAN_POWER_DOMAIN_SWITCHED_CORE);
  on = powman_power_state_with_domain_on(on, POWMAN_POWER_DOMAIN_XIP_CACHE);
  const powman_power_state off = POWMAN_POWER_STATE_NONE;
  if (powman_configure_wakeup_state(off, on)) {
    for (int i = 0; i < 4; ++i) powman_hw->boot[i] = 0;
    watchdog_update();
    enter_power_down(off);
  }
  // Refused (a button pressed meanwhile is a pending wake request, or an
  // invalid state): the clocks, pads and USB are no longer as main() set
  // them up. Reboot as a wake instead of emulating sleep on a half-configured
  // chip (an intentional reset, not a crash); the wake button is then
  // captured at boot as after a real power-down.
  diag::reboot(diag::ResetKind::SleepWake);
}

// Nothing to undo: power_off() returns only before changing anything.
void cancel_power_off() {}

}  // namespace badge::board
