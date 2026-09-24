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
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_game_art.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_game_art.h
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_game_model.h
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_game_scan.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_game_scan.h
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_host.h
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_library_store.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_library_store.h
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_profile_dialog.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_profile_dialog.h
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_library_view.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_library_view.h
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_patch_dialog.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/wx_patch_dialog.h
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
