# BHIHKH! hardware target selection. Included by the firmware CMakeLists.txt
# before the Pico SDK is imported: the target decides PICO_BOARD and
# PICO_PLATFORM.
#
#   -DBHIHKH_TARGET=badger2040   original Badger 2040 (RP2040); the default
#   -DBHIHKH_TARGET=badger2350   Badger 2350 (RP2350A)
#
# The target list is cmake/bhihkh_targets_list.cmake. Each target has
# firmware/platform/<target>/target.cmake, which sets (all paths absolute):
#   BHIHKH_PICO_BOARD, BHIHKH_PICO_PLATFORM   handed to the Pico SDK
#   BHIHKH_TARGET_DESCRIPTION                  program description (picotool info)
#   BHIHKH_TARGET_INCLUDE_DIRS                 backend headers
#   BHIHKH_TARGET_SOURCES                      backend sources, in link order
#   BHIHKH_TARGET_VENDOR_SOURCES               third-party sources (built with -w)
#   BHIHKH_TARGET_DEFINITIONS                  board compile definitions
#   BHIHKH_TARGET_LIBRARIES                    Pico SDK libraries the backend uses
#   BHIHKH_TARGET_ARTIFACT_PREFIX              artifact file name prefix (names the board)
# Selecting an unknown target fails here, before anything is built; there is
# no fallback to another board.
include(${CMAKE_CURRENT_LIST_DIR}/bhihkh_targets_list.cmake)

set(BHIHKH_TARGET badger2040 CACHE STRING "BHIHKH! hardware target (implemented: ${BHIHKH_TARGETS_IMPLEMENTED})")
set_property(CACHE BHIHKH_TARGET PROPERTY STRINGS ${BHIHKH_TARGETS_IMPLEMENTED})

if(NOT BHIHKH_TARGET IN_LIST BHIHKH_TARGETS_IMPLEMENTED)
  message(FATAL_ERROR "unknown BHIHKH_TARGET '${BHIHKH_TARGET}'; implemented: ${BHIHKH_TARGETS_IMPLEMENTED}")
endif()
# A build directory belongs to one board: its SDK configuration (chip, board
# header, toolchain flags) cannot be switched in place.
if(DEFINED CACHE{BHIHKH_TARGET_CONFIGURED} AND NOT "$CACHE{BHIHKH_TARGET_CONFIGURED}" STREQUAL "${BHIHKH_TARGET}")
  message(FATAL_ERROR "this build directory is configured for BHIHKH_TARGET=$CACHE{BHIHKH_TARGET_CONFIGURED}; "
                      "build ${BHIHKH_TARGET} in a separate directory (scripts/build-firmware.sh uses "
                      "build/fw-<target> for targets other than the default)")
endif()
set(BHIHKH_TARGET_CONFIGURED ${BHIHKH_TARGET} CACHE INTERNAL "BHIHKH_TARGET this build directory was configured for")

get_filename_component(BHIHKH_TARGET_DIR ${CMAKE_CURRENT_LIST_DIR}/../firmware/platform/${BHIHKH_TARGET} ABSOLUTE)
include(${BHIHKH_TARGET_DIR}/target.cmake)

# The board follows from the target. A PICO_BOARD or PICO_PLATFORM given on
# the command line (or left in the cache) for other hardware would otherwise
# be ignored or half-applied, so refuse it.
foreach(_var PICO_BOARD PICO_PLATFORM)
  if(DEFINED CACHE{${_var}} AND NOT "$CACHE{${_var}}" STREQUAL "${BHIHKH_${_var}}")
    message(FATAL_ERROR "${_var}=$CACHE{${_var}} conflicts with BHIHKH_TARGET=${BHIHKH_TARGET} "
                        "(${BHIHKH_${_var}}). Select the hardware with BHIHKH_TARGET only.")
  endif()
endforeach()
set(PICO_BOARD ${BHIHKH_PICO_BOARD} CACHE STRING "Pico SDK board (follows BHIHKH_TARGET)")
set(PICO_PLATFORM ${BHIHKH_PICO_PLATFORM})
message(STATUS "BHIHKH! target: ${BHIHKH_TARGET} (PICO_BOARD=${PICO_BOARD}, PICO_PLATFORM=${PICO_PLATFORM})")
