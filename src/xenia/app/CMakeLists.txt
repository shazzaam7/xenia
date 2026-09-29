# src/xenia/app/CMakeLists.txt - Main xenia_canary executable.

if(WIN32)
  add_executable(xenia-app WIN32)
else()
  add_executable(xenia-app)
endif()

set_target_properties(xenia-app PROPERTIES OUTPUT_NAME "xenia_canary_netplay")

# Main source
target_sources(xenia-app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/xenia_main.cc
)

# Other app sources (emulator_window, profile_dialogs, etc.)
xe_platform_sources(xenia-app ${CMAKE_CURRENT_SOURCE_DIR})

# Platform-specific entry points
if(WIN32)
  target_sources(xenia-app PRIVATE
    ${PROJECT_SOURCE_DIR}/src/xenia/base/main_init_win.cc
    ${PROJECT_SOURCE_DIR}/src/xenia/ui/windowed_app_main_win.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/main_resources.rc
    ${PROJECT_SOURCE_DIR}/src/xenia/base/app_win32.manifest
  )
  if(XE_TARGET_X86_64)
    # main_init_win.cc needs SSE2 only (not AVX)
    set_source_files_properties(
      ${PROJECT_SOURCE_DIR}/src/xenia/base/main_init_win.cc
      PROPERTIES COMPILE_OPTIONS "/arch:SSE2"
    )
  endif()
else()
  target_sources(xenia-app PRIVATE
    ${PROJECT_SOURCE_DIR}/src/xenia/base/main_init_posix.cc
    ${PROJECT_SOURCE_DIR}/src/xenia/ui/windowed_app_main_posix.cc
  )
  if(NOT MSVC AND XE_TARGET_X86_64)
    set_source_files_properties(
      ${PROJECT_SOURCE_DIR}/src/xenia/base/main_init_posix.cc
      PROPERTIES COMPILE_OPTIONS "-msse2;-mno-avx"
    )
  endif()
endif()

target_compile_definitions(xenia-app PRIVATE
  XBYAK_NO_OP_NAMES
  XBYAK_ENABLE_OMITTED_OPERAND
  CURL_STATICLIB
)

# CA bundle embedded into the binary (see cmake/EmbedCaBundle.cmake): generated
# at configure time and rebuilt whenever assets/cacert.pem changes. The bundle
# is authoritative at runtime. The script emits an empty bundle when the file
# is absent, and HTTPS downloads then fail closed.
set(_cacert_pem "${PROJECT_SOURCE_DIR}/assets/cacert.pem")
set(_cacert_gen "${CMAKE_CURRENT_BINARY_DIR}/cacert_bundle.cc")
execute_process(COMMAND ${CMAKE_COMMAND}
  -DCACERT_PEM=${_cacert_pem} -DOUTPUT=${_cacert_gen}
  -P "${PROJECT_SOURCE_DIR}/cmake/EmbedCaBundle.cmake")
if(EXISTS "${_cacert_pem}")
  add_custom_command(
    OUTPUT ${_cacert_gen}
    COMMAND ${CMAKE_COMMAND}
      -DCACERT_PEM=${_cacert_pem} -DOUTPUT=${_cacert_gen}
      -P "${PROJECT_SOURCE_DIR}/cmake/EmbedCaBundle.cmake"
    DEPENDS ${_cacert_pem}
    COMMENT "Embedding CA bundle")
else()
  if(WIN32)
    # Schannel uses the OS trust store; the bundle is only needed for
    # non-Windows TLS.
    message(STATUS
      "assets/cacert.pem not found: skipping embedded CA bundle "
      "(Schannel builds don't need it).")
  else()
    # The bundle is fetched by `xb ca-bundle`, not vendored. Surfacing
    # this at configure time beats an opaque runtime TLS failure on non-Windows.
    message(WARNING
      "assets/cacert.pem not found: no CA bundle will be embedded. Run "
      "`xb ca-bundle` (or place the bundle there) or HTTPS downloads "
      "will fail cert verification on non-Windows builds.")
  endif()
endif()
target_sources(xenia-app PRIVATE ${_cacert_gen})

# Core libraries (all platforms)
target_link_libraries(xenia-app PRIVATE
  xenia-apu
  xenia-apu-nop
  xenia-base
  xenia-core
  xenia-cpu
  xenia-gpu
  xenia-gpu-null
  xenia-hid
  xenia-hid-nop
  xenia-kernel
  xenia-patcher
  xenia-ui
  xenia-vfs
  # Third-party
  aes_128
  capstone
  fmt
  dxbc
  discord-rpc
  imgui
  libavcodec
  libavutil
  mspack
  snappy
  xxhash
  libcurl
)

# Vulkan/SPIR-V backends (not macOS)
if(NOT APPLE)
  target_link_libraries(xenia-app PRIVATE
    xenia-gpu-vulkan
    xenia-ui-vulkan
    glslang-spirv
  )
endif()

# Non-Android specific (always true for Windows/Linux)
target_link_libraries(xenia-app PRIVATE
  xenia-app-discord
  xenia-apu-sdl
  xenia-debug-ui
  xenia-helper-sdl
  xenia-hid-sdl
  xenia-hid-keyboard
)

# Architecture-specific backend
if(XE_TARGET_X86_64)
  target_link_libraries(xenia-app PRIVATE xenia-cpu-backend-x64)
elseif(XE_TARGET_AARCH64)
  target_link_libraries(xenia-app PRIVATE xenia-cpu-backend-a64)
endif()

# Platform-specific libraries
if(WIN32)
  target_link_libraries(xenia-app PRIVATE
    xenia-apu-xaudio2
    xenia-gpu-d3d12
    xenia-hid-xinput
    xenia-ui-d3d12
  )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_libraries(xenia-app PRIVATE
    xenia-apu-alsa
    X11
    xcb
    X11-xcb
    SDL2
  )
endif()

xe_target_defaults(xenia-app)
