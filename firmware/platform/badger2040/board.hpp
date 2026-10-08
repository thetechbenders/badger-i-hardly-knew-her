// Badger 2040 (RP2040) board support. Pin numbers come from the Pico SDK
// board header boards/pimoroni_badger2040.h (PICO_BOARD=pimoroni_badger2040).
//
// Power: on battery the 3V3 regulator is enabled either by a front button
// (through diodes) or by GPIO10 (BADGER2040_3V3_EN_PIN). Firmware must drive
// GPIO10 high immediately at boot to stay on after the button is released,
// and drives it low to power off. With USB connected the board stays powered
// regardless, so power-off is emulated (see power_off()).
// There is no RTC and no charger on the original Badger 2040.
//
// Every target's board.hpp provides the same interface; the shared Pico SDK
// backend (firmware/platform/pico/) is written against it.
#pragma once

#include <cstdint>

#include "battery.hpp"
#include "crash_record.hpp"
#include "input.hpp"
#include "panel_uc8151.hpp"
#include "pico.h"

namespace badge::board {

// ------------------------------------------------------------ board facts
constexpr const char *kBoardName = "Badger 2040";
constexpr const char *kChip = "RP2040";
constexpr const char *kPsram = "no PSRAM";
constexpr const char *kPanelName = "UC8151 296x128";
using Panel = Uc8151Panel;
constexpr bool kPanelHasSpeeds = true;  // refresh.speed picks one of four waveforms (0 = OTP, cleanest)
// Qwiic connector (and nothing else) on I2C0.
constexpr int kI2cInstance = BADGER2040_I2C;
constexpr unsigned kI2cSdaPin = BADGER2040_SDA_PIN, kI2cSclPin = BADGER2040_SCL_PIN;
// 1.24 V reference, battery sense through a 1/3 divider (battery.hpp).
constexpr BatteryCircuit kBatteryCircuit = kBadger2040Battery;
// What the battery-sense reading means while VBUS is present.
constexpr const char *kUsbSenseNote = "reflects USB, not the cell";
constexpr bool kHasActivityLed = true;  // front LED, lit while the panel refreshes

// Level of each front button captured by the earliest boot constructor
// (bit i = badge::Button i). USR is excluded: its default pad pull-down
// makes it read "pressed" before init.
uint32_t wake_buttons();
// First wake button in A, B, C, UP, DOWN order, or -1.
int wake_button();

// Reset cause, for diag::classify_boot().
diag::ResetFlags reset_flags();

void init();
uint32_t read_buttons();  // bit i = Button i pressed (debounce elsewhere)
bool usb_powered();       // VBUS detect
// Raw battery-sense and 1.24 V reference ADC counts (see battery.hpp for the
// conversion). Enables the reference only for the measurement (~1 ms).
BatteryRaw read_battery_raw();
void led(uint8_t level);  // PWM, 0..255
// Drop the power latch. Returns only if the board is still powered (USB
// connected or a button held), after which the caller emulates sleep and
// calls cancel_power_off() before rebooting on a button press.
void power_off();
void cancel_power_off();

}  // namespace badge::board
