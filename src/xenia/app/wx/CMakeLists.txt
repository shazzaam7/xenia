# src/xenia/app/wx/CMakeLists.txt - wxWidgets UI backend for xenia-app.
# Built only when wxWidgets resolved (see third_party/CMakeLists.txt).

if(NOT XENIA_WX_AVAILABLE)
  return()
endif()

# OBJECT (not STATIC): wx_app.cc only registers WxApp via a static
# wxAppInitializer consumed by wxEntry(). With a static archive the linker
# would discard that TU (nothing references its symbols) and wxEntry() would
# fall back to wxDummyConsoleApp. OBJECT members are always linked.
add_library(xenia-app-wx OBJECT
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_app.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_app.h
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_content_install_dialog.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_content_install_dialog.h
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_console_settings_dialog.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_console_settings_dialog.h
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_config_editor_dialog.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_config_editor_dialog.h
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_config_row.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_config_row.h
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_game_config_dialog.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_game_config_dialog.h
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_game_content_dialog.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_game_content_dialog.h
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_game_info_dialog.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_game_info_dialog.h
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_game_art.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_game_art.h
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_game_model.h
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_game_scan.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_game_scan.h
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_compat_db.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_compat_db.h
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_host.h
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_library_store.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_library_store.h
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_profile_dialog.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_profile_dialog.h
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_library_view.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_library_view.h
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_patch_dialog.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_patch_dialog.h
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_patch_update.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_patch_update.h
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_util.h
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_window.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_window.h
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_window_priv.h
)
if(WIN32)
  target_sources(xenia-app-wx PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/wx_window_win.cc
  )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_sources(xenia-app-wx PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/wx_window_linux.cc
  )
endif()
# PUBLIC: the headers include wxWidgets headers (and platform headers on
# Windows).
target_link_libraries(xenia-app-wx PUBLIC wx::core wx::base)
# rapidjson is header-only (parses compatibility_data.json).
target_include_directories(xenia-app-wx PRIVATE
  "${PROJECT_SOURCE_DIR}/third_party/rapidjson/include")
# CA bundle embedded into the binary (see cmake/EmbedCaBundle.cmake): generated
# at configure time and rebuilt whenever assets/cacert.pem changes. The bundle
# is authoritative at runtime — no sidecar copy is produced, so updating roots
# means re-running `xb ca-bundle` and rebuilding. The script emits an
# empty bundle when the file is absent, and HttpGet then falls back to a
# cacert.pem found on disk; with neither, verification fails closed.
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
  # The bundle is fetched by `xb ca-bundle`, not vendored. Surfacing
  # this at configure time beats an opaque runtime TLS failure on non-Windows
  # (where wolfSSL has no OS trust store); Windows/Schannel is unaffected.
  message(WARNING
    "assets/cacert.pem not found: no CA bundle will be embedded. Run "
    "`xb ca-bundle` (or place the bundle there) or HTTPS downloads "
    "will fail cert verification on non-Windows builds.")
endif()
# Refresh at build time so `xb ca-bundle` followed by `xb build --no_premake`
# (no reconfigure) still embeds the new bundle. Cheap (small script) and
# always out-of-date, so it runs on every build before compiling.
add_custom_target(xenia-cacert-refresh
  COMMAND ${CMAKE_COMMAND}
    -DCACERT_PEM=${_cacert_pem} -DOUTPUT=${_cacert_gen}
    -P "${PROJECT_SOURCE_DIR}/cmake/EmbedCaBundle.cmake"
  COMMENT "Refreshing embedded CA bundle")
add_dependencies(xenia-app-wx xenia-cacert-refresh)
target_sources(xenia-app-wx PRIVATE ${_cacert_gen})
# libcurl powers the compatibility-data download. Prefer the vendored
# static build, fall back to system curl, else degrade to cache-only
# (everything reads as Unknown until a cache file exists).
# When the vendored build is wolfSSL-backed (XENIA_CURL_TLS_REQUIRED) the
# system fallback is skipped: a system OpenSSL curl would silently replace
# the intended TLS stack.
if(NOT TARGET CURL::libcurl AND NOT XENIA_CURL_TLS_REQUIRED)
  find_package(CURL QUIET)
endif()
if(TARGET CURL::libcurl)
  target_compile_definitions(xenia-app-wx PRIVATE XENIA_HAS_CURL)
  target_link_libraries(xenia-app-wx PRIVATE CURL::libcurl)
elseif(CURL_FOUND)
  target_compile_definitions(xenia-app-wx PRIVATE XENIA_HAS_CURL)
  target_include_directories(xenia-app-wx PRIVATE ${CURL_INCLUDE_DIRS})
  target_link_libraries(xenia-app-wx PRIVATE ${CURL_LIBRARIES})
else()
  message(STATUS "xenia-app-wx: libcurl not found, compatibility download disabled (cache-only)")
endif()
if(WIN32)
  target_link_libraries(xenia-app-wx PRIVATE dwmapi)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_libraries(xenia-app-wx PRIVATE X11 xcb X11-xcb ${GTK3_LIBRARIES})
endif()
# Pull wxWidgets headers in as system headers so they don't trip -Werror
# (Clang) in TUs including them, here and in consumers (xenia-app, demos).
# MSVC builds are already clean without this.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  foreach(_wx_lib wxcore wxbase)
    if(TARGET ${_wx_lib})
      get_target_property(_wx_inc_dirs ${_wx_lib} INTERFACE_INCLUDE_DIRECTORIES)
      if(_wx_inc_dirs)
        target_include_directories(xenia-app-wx SYSTEM INTERFACE ${_wx_inc_dirs})
      endif()
    endif()
  endforeach()
endif()
xe_target_defaults(xenia-app-wx)
