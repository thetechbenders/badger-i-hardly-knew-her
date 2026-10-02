# BHIHKH! photo badge + business card firmware (native Pico SDK).
#
#   scripts/fetch-deps.sh
#   cmake -S . -B build/fw -G Ninja -DCMAKE_BUILD_TYPE=Release [-DBHIHKH_TARGET=badger2040]
#   cmake --build build/fw
#
# BHIHKH_TARGET selects the hardware (cmake/bhihkh_target.cmake); badger2040,
# the original Badger 2040, is the default and the only implemented target.
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
include(cmake/bhihkh_target.cmake)  # sets PICO_BOARD, PICO_PLATFORM, BHIHKH_TARGET_*
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
  # Fixed abbreviation: the default grows with the object count, so a fresh
  # clone (or a restored bundle) could embed a different string.
  execute_process(COMMAND ${GIT_EXECUTABLE} describe --always --dirty --tags --abbrev=12
                  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} OUTPUT_VARIABLE BADGER_GIT_VERSION
                  OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  execute_process(COMMAND ${GIT_EXECUTABLE} log -1 --format=%cI
                  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} OUTPUT_VARIABLE BADGER_BUILD_DATE
                  OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  # Git versions disagree on how %cI writes UTC ("+00:00" or "Z"); one
  # spelling keeps the image byte-identical across build machines.
  string(REGEX REPLACE "[+-]00:00$" "Z" BADGER_BUILD_DATE "${BADGER_BUILD_DATE}")
endif()
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/deps.lock _tag REGEX "^PIMORONI_PICO_TAG=")
string(REPLACE "PIMORONI_PICO_TAG=" "" PIMORONI_PICO_VERSION "${_tag}")

include(cmake/badger_content.cmake)
badger_generate_content(${CMAKE_CURRENT_BINARY_DIR}/generated)
get_filename_component(BADGER_PROFILE_NAME ${BADGER_PROFILE} NAME)
configure_file(firmware/platform/build_info.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/generated/build_info.cpp @ONLY)

get_filename_component(BADGER_SDK_REAL "${PICO_SDK_PATH}" REALPATH)
get_filename_component(BADGER_PIMORONI_REAL "${PIMORONI_PICO_PATH}" REALPATH)

set(FW badger_badge)
add_executable(${FW}
  ${BADGER_CORE_SOURCES}
  ${BADGER_CLI_SOURCES}
  ${BADGER_GENERATED_SOURCES}
  ${CMAKE_CURRENT_BINARY_DIR}/generated/build_info.cpp
  ${BHIHKH_TARGET_SOURCES}
  ${BHIHKH_TARGET_VENDOR_SOURCES}
)
target_include_directories(${FW} PRIVATE
  firmware/core firmware/cli ${BHIHKH_TARGET_INCLUDE_DIRS} firmware/platform third_party/qrcodegen
  ${PIMORONI_PICO_PATH})
target_compile_options(${FW} PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wno-unused-parameter -fno-exceptions -fno-rtti>
  # Reproducible across checkout locations: no absolute source paths in
  # __FILE__ strings or debug info (applies to the SDK sources built here too).
  -ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=.
  # pico_sdk_import resolves PICO_SDK_PATH to its real path, so an SDK outside
  # the tree (a deps symlink, PICO_SDK_PATH in the environment) must map to
  # the same names an in-tree deps/ gets from the rule above.
  -ffile-prefix-map=${BADGER_SDK_REAL}=./deps/pico-sdk
  -ffile-prefix-map=${BADGER_PIMORONI_REAL}=./deps/pimoroni-pico
  # Reproducible across build-directory names too. The compile directory
  # (DW_AT_comp_dir) reads as ".", so the relative source names above resolve
  # from the repository root; generated sources and SDK autogen headers read
  # as ./build/fw/generated/..., where a default build puts them. GCC applies
  # the last matching map, so these come last (the build tree usually lies
  # inside the source tree) and the more specific one goes after.
  -ffile-prefix-map=${CMAKE_BINARY_DIR}=.
  -ffile-prefix-map=${CMAKE_BINARY_DIR}/generated=./build/fw/generated)
set_source_files_properties(${BHIHKH_TARGET_VENDOR_SOURCES}
                            third_party/qrcodegen/qrcodegen.c PROPERTIES COMPILE_OPTIONS "-w")
target_compile_definitions(${FW} PRIVATE
  PICO_PANIC_FUNCTION=badge_panic
  PICO_STACK_SIZE=0x1000
  PICO_CORE1_STACK_SIZE=0x1000
  PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS=0
  ${BHIHKH_TARGET_DEFINITIONS}
)
target_link_libraries(${FW} PRIVATE ${BHIHKH_TARGET_LIBRARIES})
pico_enable_stdio_usb(${FW} 1)
pico_enable_stdio_uart(${FW} 0)
pico_set_program_name(${FW} "BHIHKH!")
pico_set_program_version(${FW} "${BADGER_GIT_VERSION}")
pico_set_program_description(${FW} "${BHIHKH_TARGET_DESCRIPTION}")
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
          --size ${BADGER_SIZE_TOOL} --nm ${BADGER_NM_TOOL} --target ${BHIHKH_TARGET}
          --out ${CMAKE_CURRENT_BINARY_DIR}/memory-report.txt
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/generated/assets.uf2
          ${CMAKE_CURRENT_BINARY_DIR}/${FW}-assets.uf2
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/checksums.py ${CMAKE_CURRENT_BINARY_DIR}
          ${FW}.uf2 ${FW}.elf ${FW}.elf.map ${FW}.bin ${FW}-assets.uf2 memory-report.txt
  VERBATIM)
