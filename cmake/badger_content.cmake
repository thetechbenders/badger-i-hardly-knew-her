# Shared between the firmware and host builds: core source list and the
# generated content (profile defaults + built-in asset pack).
#
# Content selection (first match wins):
#   -DBADGER_PROFILE=<json>      else local/profile.json if present, else config/sample-profile.json
#                                A .toml form supplies the profile only (its [portrait] needs
#                                converting first): -DBADGER_PORTRAIT is then required, and
#                                configuring fails without it. scripts/build-badge.sh does both.
#   -DBADGER_PORTRAIT=<png>      else local/portrait.png if present, else assets/sample/portrait_placeholder.png
#   -DBADGER_PORTRAIT=none       build without a compiled-in portrait
get_filename_component(_BADGER_ROOT ${CMAKE_CURRENT_LIST_DIR}/.. ABSOLUTE)

set(BADGER_CORE_SOURCES
  ${_BADGER_ROOT}/firmware/core/apds9960.cpp
  ${_BADGER_ROOT}/firmware/core/app.cpp
  ${_BADGER_ROOT}/firmware/core/assetpack.cpp
  ${_BADGER_ROOT}/firmware/core/battery.cpp
  ${_BADGER_ROOT}/firmware/core/crash_record.cpp
  ${_BADGER_ROOT}/firmware/core/crc32.cpp
  ${_BADGER_ROOT}/firmware/core/display_pipeline.cpp
  ${_BADGER_ROOT}/firmware/core/framebuffer.cpp
  ${_BADGER_ROOT}/firmware/core/gesture.cpp
  ${_BADGER_ROOT}/firmware/core/input.cpp
  ${_BADGER_ROOT}/firmware/core/qr.cpp
  ${_BADGER_ROOT}/firmware/core/renderer.cpp
  ${_BADGER_ROOT}/firmware/core/settings.cpp
  ${_BADGER_ROOT}/firmware/core/settings_store.cpp
  ${_BADGER_ROOT}/firmware/core/text.cpp
  ${_BADGER_ROOT}/firmware/generated/fonts.cpp
  ${_BADGER_ROOT}/firmware/generated/icons.cpp
  ${_BADGER_ROOT}/third_party/qrcodegen/qrcodegen.c
)
set(BADGER_CLI_SOURCES ${_BADGER_ROOT}/firmware/cli/cli.cpp)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

function(badger_generate_content OUT_DIR)
  if(NOT BADGER_PROFILE)
    if(EXISTS ${_BADGER_ROOT}/local/profile.json)
      set(BADGER_PROFILE ${_BADGER_ROOT}/local/profile.json)
    else()
      set(BADGER_PROFILE ${_BADGER_ROOT}/config/sample-profile.json)
    endif()
  endif()
  set(_portrait_defaulted FALSE)
  if(NOT BADGER_PORTRAIT)
    set(_portrait_defaulted TRUE)
    if(EXISTS ${_BADGER_ROOT}/local/portrait.png)
      set(BADGER_PORTRAIT ${_BADGER_ROOT}/local/portrait.png)
    else()
      set(BADGER_PORTRAIT ${_BADGER_ROOT}/assets/sample/portrait_placeholder.png)
    endif()
  endif()
  message(STATUS "Badge profile:  ${BADGER_PROFILE}")
  message(STATUS "Badge portrait: ${BADGER_PORTRAIT}")
  set(BADGER_PROFILE ${BADGER_PROFILE} PARENT_SCOPE)
  set(BADGER_PORTRAIT ${BADGER_PORTRAIT} PARENT_SCOPE)

  # A fill-in form reads other files (vCard, portrait settings): regenerate
  # the profile when any of them changes, and re-configure when the form does.
  set(_profile_deps ${BADGER_PROFILE})
  if(BADGER_PROFILE MATCHES "\\.toml$")
    if(_portrait_defaulted)
      message(FATAL_ERROR "BADGER_PROFILE is a form (${BADGER_PROFILE}). CMake reads only its profile; "
                          "the form's [portrait] must be converted first, so a default portrait "
                          "(${BADGER_PORTRAIT}) is not used. Build with scripts/build-badge.sh <form>, "
                          "or pass -DBADGER_PORTRAIT=<1-bit png> explicitly.")
    endif()
    execute_process(COMMAND ${Python3_EXECUTABLE} ${_BADGER_ROOT}/tools/badge_form.py inputs ${BADGER_PROFILE}
                    OUTPUT_VARIABLE _form_inputs ERROR_VARIABLE _form_err RESULT_VARIABLE _form_rc
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT _form_rc EQUAL 0)
      message(FATAL_ERROR "form ${BADGER_PROFILE} does not validate:${_form_err}")
    endif()
    string(REPLACE "\n" ";" _form_inputs "${_form_inputs}")
    list(APPEND _profile_deps ${_form_inputs})
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${BADGER_PROFILE})
  endif()

  file(MAKE_DIRECTORY ${OUT_DIR})
  add_custom_command(
    OUTPUT ${OUT_DIR}/profile_defaults.cpp
    COMMAND ${Python3_EXECUTABLE} ${_BADGER_ROOT}/tools/profilegen.py ${BADGER_PROFILE} --out ${OUT_DIR}/profile_defaults.cpp
    DEPENDS ${_profile_deps} ${_BADGER_ROOT}/tools/profilegen.py ${_BADGER_ROOT}/tools/badge_profile.py
            ${_BADGER_ROOT}/tools/badge_form.py
    COMMENT "Generating profile defaults from ${BADGER_PROFILE}")
  set(_portrait_args)
  set(_portrait_deps)
  if(NOT BADGER_PORTRAIT STREQUAL "none")
    set(_portrait_args --portrait ${BADGER_PORTRAIT})
    set(_portrait_deps ${BADGER_PORTRAIT})
  endif()
  add_custom_command(
    OUTPUT ${OUT_DIR}/builtin_assets.cpp ${OUT_DIR}/builtin_assets.bin ${OUT_DIR}/assets.uf2
    COMMAND ${Python3_EXECUTABLE} ${_BADGER_ROOT}/tools/assetpack.py build ${_portrait_args}
            --out ${OUT_DIR}/builtin_assets.bin --cpp ${OUT_DIR}/builtin_assets.cpp --uf2 ${OUT_DIR}/assets.uf2
    DEPENDS ${_portrait_deps} ${_BADGER_ROOT}/tools/assetpack.py ${_BADGER_ROOT}/tools/uf2.py
    COMMENT "Building built-in asset pack")
  set(BADGER_GENERATED_SOURCES ${OUT_DIR}/profile_defaults.cpp ${OUT_DIR}/builtin_assets.cpp PARENT_SCOPE)
endfunction()
