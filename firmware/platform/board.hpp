// Badger 2040 (RP2040) board support. Pin numbers come from the Pico SDK
// board header boards/pimoroni_badger2040.h (PICO_BOARD=pimoroni_badger2040).
//
// Power: on battery the 3V3 regulator is enabled either by a front button
// (through diodes) or by GPIO10 (BADGER2040_3V3_EN_PIN). Firmware must drive
// GPIO10 high immediately at boot to stay on after the button is released,
// and drives it low to power off. With USB connected the board stays powered
// regardless, so power-off is emulated (see Board::power_off()).
// There is no RTC and no charger on the original Badger 2040.
#pragma once

#include <cstdint>

#include "input.hpp"

namespace badge::board {

// Level of each front button captured by the earliest boot constructor
// (bit i = badge::Button i). USR is excluded: its default pad pull-down
// makes it read "pressed" before init.
uint32_t wake_buttons();
// First wake button in A, B, C, UP, DOWN order, or -1.
int wake_button();

void init();
uint32_t read_buttons();  // bit i = Button i pressed (debounce elsewhere)
bool usb_powered();       // VBUS detect
// Battery/VSYS estimate in millivolts using the on-board 1.24 V reference.
uint32_t vsys_mv();
void led(uint8_t level);  // PWM, 0..255
// Drop the power latch. Returns only if the board is still powered (USB
// connected or a button held), after which the caller emulates sleep.
void release_power_latch();
void hold_power_latch();

}  // namespace badge::board
