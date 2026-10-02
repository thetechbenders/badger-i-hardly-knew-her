# BHIHKH! hardware target selection. Included by the firmware CMakeLists.txt
# before the Pico SDK is imported: the target decides PICO_BOARD and
# PICO_PLATFORM.
#
#   -DBHIHKH_TARGET=badger2040   original Badger 2040 (RP2040); the default
#   -DBHIHKH_TARGET=badger2350   Badger 2350: planned, not implemented yet
#
# An implemented target has firmware/platform/<target>/target.cmake, which
# sets (all paths absolute):
#   BHIHKH_PICO_BOARD, BHIHKH_PICO_PLATFORM   handed to the Pico SDK
#   BHIHKH_TARGET_DESCRIPTION                  program description (picotool info)
#   BHIHKH_TARGET_INCLUDE_DIRS                 backend headers
#   BHIHKH_TARGET_SOURCES                      backend sources, in link order
#   BHIHKH_TARGET_VENDOR_SOURCES               third-party sources (built with -w)
#   BHIHKH_TARGET_DEFINITIONS                  board compile definitions
#   BHIHKH_TARGET_LIBRARIES                    Pico SDK libraries the backend uses
# Selecting a planned or unknown target fails here, before anything is built.
set(BHIHKH_TARGETS_IMPLEMENTED badger2040)
set(BHIHKH_TARGETS_PLANNED badger2350)

set(BHIHKH_TARGET badger2040 CACHE STRING "BHIHKH! hardware target (implemented: ${BHIHKH_TARGETS_IMPLEMENTED})")
set_property(CACHE BHIHKH_TARGET PROPERTY STRINGS ${BHIHKH_TARGETS_IMPLEMENTED})

if(BHIHKH_TARGET IN_LIST BHIHKH_TARGETS_PLANNED)
  message(FATAL_ERROR "BHIHKH_TARGET=${BHIHKH_TARGET} is planned but not implemented yet; "
                      "the only buildable target is badger2040 (the default). "
                      "No firmware is configured for it.")
elseif(NOT BHIHKH_TARGET IN_LIST BHIHKH_TARGETS_IMPLEMENTED)
  message(FATAL_ERROR "unknown BHIHKH_TARGET '${BHIHKH_TARGET}'; "
                      "implemented: ${BHIHKH_TARGETS_IMPLEMENTED}; planned: ${BHIHKH_TARGETS_PLANNED}")
endif()

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
