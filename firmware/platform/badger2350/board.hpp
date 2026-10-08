// Badger 2350 (RP2350A) board support. Pin numbers come from the Pico SDK
// board header boards/pimoroni_badger2350.h (PICO_BOARD=pimoroni_badger2350,
// pico-sdk 2.3.1); polarities, pulls and the power-down sequence follow
// Pimoroni's BadgeWare firmware (pimoroni/badger2350 v3.1.1,
// modules/c/powman/powman.c), used as a hardware reference only.
//
// Buttons A, B, C, UP, DOWN and HOME are active low and need the RP2350's
// internal pull-ups. GPIO15 (BUTTON_INT) goes low while any of A..DOWN is
// pressed; it is the wake source.
//
// Power: there is no power latch. The battery always feeds the chip; "off"
// is the RP2350 power manager's lowest state (switched core, SRAM and XIP
// powered down), left again by a front-button press, which reboots the chip
// from flash (RAM contents lost, like a Badger 2040 battery wake). With USB
// connected power-off is emulated, as on the Badger 2040 (see power_off()).
//
// The board's wireless chip, RTC alarm, rear LEDs and PSRAM are not used by
// this firmware. The charger works by itself; its status line is on the
// wireless chip, so the firmware never reports charging (battery.hpp).
//
// Every target's board.hpp provides the same interface; the shared Pico SDK
// backend (firmware/platform/pico/) is written against it.
#pragma once

#include <cstdint>

#include "battery.hpp"
#include "crash_record.hpp"
#include "input.hpp"
#include "panel_ssd1680.hpp"
#include "pico.h"

namespace badge::board {

// ------------------------------------------------------------ board facts
constexpr const char *kBoardName = "Badger 2350";
constexpr const char *kChip = "RP2350A";
constexpr const char *kPsram = "8 MiB PSRAM on board, not initialised or used";
constexpr const char *kPanelName = "SSD1680 264x176";
using Panel = Ssd1680Panel;
constexpr bool kPanelHasSpeeds = false;  // one waveform: refresh.speed and refresh.partial have no effect
// Qw/ST connector, shared with the PCF85063A RTC (address 0x51; the
// APDS-9960 gesture sensor is 0x39), on I2C0. Its supply is switched by
// GPIO27 (BADGER2350_SW_POWER_EN_PIN), which init() turns on.
constexpr int kI2cInstance = BADGER2350_RTC_I2C;
constexpr unsigned kI2cSdaPin = BADGER2350_RTC_I2C_SDA_PIN, kI2cSclPin = BADGER2350_RTC_I2C_SCL_PIN;
// 1.1 V reference (SENSE_1V1, GPIO28), battery sense through a 1/2 divider
// (VBAT_SENSE, GPIO26): BadgeWare's 2 * 1.1 V * vbat / ref.
constexpr BatteryCircuit kBatteryCircuit{1100, 2};
// While VBUS is present the cell is on the charger: its voltage reads high
// and says little about the charge left.
constexpr const char *kUsbSenseNote = "cell on the charger, reads high";
constexpr bool kHasActivityLed = false;  // no front LED (the rear case LEDs are not used)

// Level of each front button captured by the earliest boot constructor
// (bit i = badge::Button i). HOME is excluded: it cannot wake the board.
uint32_t wake_buttons();
// First wake button in A, B, C, UP, DOWN order, or -1.
int wake_button();

// Reset cause, for diag::classify_boot().
diag::ResetFlags reset_flags();

void init();
uint32_t read_buttons();  // bit i = Button i pressed (debounce elsewhere)
bool usb_powered();       // VBUS detect
// Raw battery-sense and 1.1 V reference ADC counts (see battery.hpp).
BatteryRaw read_battery_raw();
void led(uint8_t level);  // no activity LED: does nothing
// On battery: power the chip down until a front button is pressed; does not
// return. Returns at once with USB connected (or if the power manager
// refuses the request), after which the caller emulates sleep and calls
// cancel_power_off() before rebooting on a button press.
void power_off();
void cancel_power_off();

}  // namespace badge::board
