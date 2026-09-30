# Host build: portable core + tests + preview renderer (no Pico SDK needed).
cmake_minimum_required(VERSION 3.16)
project(badger_host C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
include(${REPO_ROOT}/cmake/badger_content.cmake)
badger_generate_content(${CMAKE_CURRENT_BINARY_DIR}/generated)

option(BADGER_SANITIZE "Build host tests with ASan/UBSan" ON)
set(SAN_FLAGS "")
if(BADGER_SANITIZE AND NOT MSVC)
  set(SAN_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
endif()

add_library(badger_core STATIC
  ${BADGER_CORE_SOURCES}
  ${BADGER_GENERATED_SOURCES}
)
target_include_directories(badger_core PUBLIC ${REPO_ROOT}/firmware/core ${REPO_ROOT}/third_party/qrcodegen)
target_compile_options(badger_core PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter ${SAN_FLAGS})
target_link_options(badger_core PUBLIC ${SAN_FLAGS})
# Vendored code: keep upstream as-is, only relax warnings for it.
set_source_files_properties(${REPO_ROOT}/third_party/qrcodegen/qrcodegen.c PROPERTIES COMPILE_OPTIONS "-Wno-error")

find_package(Threads REQUIRED)
file(GLOB TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_*.cpp)
add_executable(badger_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/main.cpp ${TEST_SOURCES}
  ${CMAKE_CURRENT_SOURCE_DIR}/sim/sim_panel.cpp)
target_include_directories(badger_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests ${CMAKE_CURRENT_SOURCE_DIR}/sim
  ${REPO_ROOT}/firmware/core ${REPO_ROOT}/firmware/cli)
target_sources(badger_tests PRIVATE ${BADGER_CLI_SOURCES})
target_compile_options(badger_tests PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter ${SAN_FLAGS})
target_link_libraries(badger_tests PRIVATE badger_core Threads::Threads)
target_compile_definitions(badger_tests PRIVATE REPO_ROOT="${REPO_ROOT}")

add_executable(badger_preview ${CMAKE_CURRENT_SOURCE_DIR}/preview.cpp)
target_include_directories(badger_preview PRIVATE ${REPO_ROOT}/firmware/core)
target_compile_options(badger_preview PRIVATE -Wall -Wextra -Werror ${SAN_FLAGS})
target_link_libraries(badger_preview PRIVATE badger_core)

enable_testing()
add_test(NAME unit COMMAND badger_tests)
