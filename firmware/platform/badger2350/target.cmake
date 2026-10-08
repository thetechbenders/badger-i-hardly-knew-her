# BHIHKH_TARGET=badger2350: the Badger 2350 (RP2350A, SSD1680 264x176
# panel used black/white, 16 MiB flash, LiPo charger, PCF85063A RTC, CYW43
# wireless). Included by cmake/bhihkh_target.cmake, which documents the
# variables. Only what the existing product needs is used: no wireless, RTC
# features, PSRAM or rear LEDs.
set(BHIHKH_PICO_BOARD pimoroni_badger2350)  # Pico SDK boards/pimoroni_badger2350.h (since 2.3.1)
set(BHIHKH_PICO_PLATFORM rp2350-arm-s)
set(BHIHKH_TARGET_DESCRIPTION "Photo badge, business card and portfolio for the Badger 2350")
set(BHIHKH_TARGET_ARTIFACT_PREFIX badger2350_badge)  # badger2350_badge.uf2: never confused with the Badger 2040's

# Board-specific code here, the shared Pico SDK backend in ../pico/.
get_filename_component(_pico ${CMAKE_CURRENT_LIST_DIR}/../pico ABSOLUTE)
set(BHIHKH_TARGET_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR})
set(BHIHKH_TARGET_SOURCES
  ${_pico}/main.cpp
  ${CMAKE_CURRENT_LIST_DIR}/board.cpp
  ${CMAKE_CURRENT_LIST_DIR}/panel_ssd1680.cpp
  ${_pico}/flash_pico.cpp
  ${_pico}/diagnostics.cpp
  ${_pico}/i2c_pico.cpp
)
set(BHIHKH_TARGET_VENDOR_SOURCES)  # nothing from pimoroni-pico
set(BHIHKH_TARGET_DEFINITIONS
  PICO_FLASH_SIZE_BYTES=16777216  # flash_layout.hpp kFlashSize (the board header's value)
)
set(BHIHKH_TARGET_LIBRARIES
  pico_stdlib pico_multicore pico_flash hardware_spi hardware_i2c hardware_adc hardware_flash
  hardware_watchdog hardware_powman hardware_clocks)
