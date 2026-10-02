# BHIHKH_TARGET=badger2040: the original Badger 2040 (RP2040, UC8151
# 296x128 1-bit panel, 2 MiB flash, no RTC, no charger). Included by
# cmake/bhihkh_target.cmake, which documents the variables.
set(BHIHKH_PICO_BOARD pimoroni_badger2040)  # Pico SDK boards/pimoroni_badger2040.h
set(BHIHKH_PICO_PLATFORM rp2040)
set(BHIHKH_TARGET_DESCRIPTION "Photo badge, business card and portfolio for the original Badger 2040")

set(BHIHKH_TARGET_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR})
set(BHIHKH_TARGET_SOURCES
  ${CMAKE_CURRENT_LIST_DIR}/main.cpp
  ${CMAKE_CURRENT_LIST_DIR}/board.cpp
  ${CMAKE_CURRENT_LIST_DIR}/panel_uc8151.cpp
  ${CMAKE_CURRENT_LIST_DIR}/flash_rp2040.cpp
  ${CMAKE_CURRENT_LIST_DIR}/diagnostics.cpp
  ${CMAKE_CURRENT_LIST_DIR}/i2c_rp2040.cpp
)
# Only the panel driver is taken from pimoroni-pico (MIT).
set(BHIHKH_TARGET_VENDOR_SOURCES ${PIMORONI_PICO_PATH}/drivers/uc8151_legacy/uc8151_legacy.cpp)
set(BHIHKH_TARGET_DEFINITIONS
  PICO_FLASH_SIZE_BYTES=2097152  # flash_layout.hpp kFlashSize
)
set(BHIHKH_TARGET_LIBRARIES
  pico_stdlib pico_multicore pico_flash hardware_spi hardware_i2c hardware_pwm hardware_adc hardware_flash
  hardware_watchdog)
