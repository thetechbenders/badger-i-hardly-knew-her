# The BHIHKH! hardware targets, shared by the firmware build
# (cmake/bhihkh_target.cmake) and the host build (host/CMakeLists.txt).
# Each has firmware/platform/<target>/ with target.cmake (firmware) and
# display_target.hpp (display size and layout, also used by host builds).
#   badger2040   original Badger 2040 (RP2040, UC8151 296x128); the default
set(BHIHKH_TARGETS_IMPLEMENTED badger2040)
