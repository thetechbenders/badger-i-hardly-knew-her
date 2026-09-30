# Badger 2040 (RP2040) photo badge + business card firmware.
#
#   scripts/fetch-deps.sh
#   cmake -S . -B build/fw -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/fw
#
# Host tests/previews live in host/ (separate CMake project, no SDK needed).
cmake_minimum_required(VERSION 3.16)

set(DEPS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps)
if(NOT DEFINED PICO_SDK_PATH AND NOT DEFINED ENV{PICO_SDK_PATH})
  set(PICO_SDK_PATH ${DEPS_DIR}/pico-sdk)
endif()
if(NOT DEFINED PIMORONI_PICO_PATH)
  set(PIMORONI_PICO_PATH ${DEPS_DIR}/pimoroni-pico)
endif()
if(NOT DEFINED picotool_DIR AND EXISTS ${DEPS_DIR}/picotool-install/picotool)
  set(picotool_DIR ${DEPS_DIR}/picotool-install/picotool)
endif()
set(PICO_BOARD pimoroni_badger2040 CACHE STRING "Original Badger 2040 (RP2040)")
set(PICO_PLATFORM rp2040)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(cmake/pico_sdk_import.cmake)
project(badger_i_hardly_knew_her C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

if(PICO_SDK_VERSION_STRING VERSION_LESS 2.2.0)
  message(FATAL_ERROR "pico-sdk ${PICO_SDK_VERSION_STRING} found; this project is pinned to 2.2.0 (deps.lock)")
endif()

# Build metadata (commit date keeps builds reproducible).
find_package(Git QUIET)
set(BADGER_GIT_VERSION "unknown")
set(BADGER_BUILD_DATE "unknown")
if(GIT_FOUND)
  execute_process(COMMAND ${GIT_EXECUTABLE} describe --always --dirty --tags
                  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} OUTPUT_VARIABLE BADGER_GIT_VERSION
                  OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  execute_process(COMMAND ${GIT_EXECUTABLE} log -1 --format=%cI
                  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} OUTPUT_VARIABLE BADGER_BUILD_DATE
                  OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/deps.lock _tag REGEX "^PIMORONI_PICO_TAG=")
string(REPLACE "PIMORONI_PICO_TAG=" "" PIMORONI_PICO_VERSION "${_tag}")

include(cmake/badger_content.cmake)
badger_generate_content(${CMAKE_CURRENT_BINARY_DIR}/generated)
get_filename_component(BADGER_PROFILE_NAME ${BADGER_PROFILE} NAME)
configure_file(firmware/platform/build_info.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/generated/build_info.cpp @ONLY)

set(FW badger_badge)
add_executable(${FW}
  ${BADGER_CORE_SOURCES}
  ${BADGER_CLI_SOURCES}
  ${BADGER_GENERATED_SOURCES}
  ${CMAKE_CURRENT_BINARY_DIR}/generated/build_info.cpp
  firmware/platform/main.cpp
  firmware/platform/board.cpp
  firmware/platform/panel_uc8151.cpp
  firmware/platform/flash_rp2040.cpp
  firmware/platform/diagnostics.cpp
  # Only the panel driver is taken from pimoroni-pico (MIT).
  ${PIMORONI_PICO_PATH}/drivers/uc8151_legacy/uc8151_legacy.cpp
)
target_include_directories(${FW} PRIVATE
  firmware/core firmware/cli firmware/platform third_party/qrcodegen ${PIMORONI_PICO_PATH})
target_compile_options(${FW} PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wno-unused-parameter -fno-exceptions -fno-rtti>)
set_source_files_properties(${PIMORONI_PICO_PATH}/drivers/uc8151_legacy/uc8151_legacy.cpp
                            third_party/qrcodegen/qrcodegen.c PROPERTIES COMPILE_OPTIONS "-w")
target_compile_definitions(${FW} PRIVATE
  PICO_PANIC_FUNCTION=badge_panic
  PICO_STACK_SIZE=0x1000
  PICO_CORE1_STACK_SIZE=0x1000
  PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS=0
  PICO_FLASH_SIZE_BYTES=2097152
)
target_link_libraries(${FW} PRIVATE
  pico_stdlib pico_multicore pico_flash hardware_spi hardware_pwm hardware_adc hardware_flash hardware_watchdog)
pico_enable_stdio_usb(${FW} 1)
pico_enable_stdio_uart(${FW} 0)
pico_set_program_name(${FW} "badger-i-hardly-knew-her")
pico_set_program_version(${FW} "${BADGER_GIT_VERSION}")
pico_set_program_description(${FW} "Formnext 2026 photo badge and business card for the original Badger 2040")
target_link_options(${FW} PRIVATE -Wl,--print-memory-usage)
pico_add_extra_outputs(${FW})  # .uf2 .bin .hex .dis .map

get_filename_component(_tc_dir "${CMAKE_C_COMPILER}" DIRECTORY)
find_program(BADGER_SIZE_TOOL arm-none-eabi-size HINTS "${_tc_dir}" REQUIRED)
find_program(BADGER_NM_TOOL arm-none-eabi-nm HINTS "${_tc_dir}" REQUIRED)

# Post-build: flash/RAM report and layout check (fails if the image would
# overlap the asset region), plus SHA-256 checksums of the artifacts.
add_custom_command(TARGET ${FW} POST_BUILD
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/memory_report.py
          --elf $<TARGET_FILE:${FW}> --map ${CMAKE_CURRENT_BINARY_DIR}/${FW}.elf.map
          --size ${BADGER_SIZE_TOOL} --nm ${BADGER_NM_TOOL}
          --out ${CMAKE_CURRENT_BINARY_DIR}/memory-report.txt
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/generated/assets.uf2
          ${CMAKE_CURRENT_BINARY_DIR}/${FW}-assets.uf2
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/checksums.py ${CMAKE_CURRENT_BINARY_DIR}
          ${FW}.uf2 ${FW}.elf ${FW}.elf.map ${FW}.bin ${FW}-assets.uf2 memory-report.txt
  VERBATIM)
