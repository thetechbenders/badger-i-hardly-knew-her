# The BHIHKH! hardware targets, shared by the firmware build
# (cmake/bhihkh_target.cmake) and the host build (host/CMakeLists.txt).
# Each has firmware/platform/<target>/ with target.cmake (firmware) and
# display_target.hpp (display size and layout, also used by host builds).
#   badger2040   original Badger 2040 (RP2040, UC8151 296x128); the default
#   badger2350   Badger 2350 (RP2350A, SSD1680 264x176)
set(BHIHKH_TARGETS_IMPLEMENTED badger2040 badger2350)
