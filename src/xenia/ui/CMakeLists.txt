add_library(xenia-ui STATIC)
xe_platform_sources(xenia-ui ${CMAKE_CURRENT_SOURCE_DIR})
# Exclude demo and windowed_app_main files
file(GLOB _ui_exclude   "${CMAKE_CURRENT_SOURCE_DIR}/*_demo.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/windowed_app_main_*.cc"
)
if(WIN32 AND XENIA_WX_UI AND XENIA_WX_AVAILABLE)
  # Replaced by the wxWidgets backend (src/xenia/app/wx), which provides
  # Window::Create, MenuItem::Create and FilePicker::Create.
  file(GLOB _ui_wx_exclude   "${CMAKE_CURRENT_SOURCE_DIR}/window_win.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/file_picker_win.cc"
  )
  list(APPEND _ui_exclude ${_ui_wx_exclude})
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND XENIA_WX_UI AND
       XENIA_WX_AVAILABLE)
  # Same replacement for the GTK backend.
  file(GLOB _ui_wx_exclude   "${CMAKE_CURRENT_SOURCE_DIR}/window_gtk.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/file_picker_gtk.cc"
  )
  list(APPEND _ui_exclude ${_ui_wx_exclude})
endif()
get_target_property(_ui_srcs xenia-ui SOURCES)
if(_ui_exclude AND _ui_srcs)
  list(REMOVE_ITEM _ui_srcs ${_ui_exclude})
  set_target_properties(xenia-ui PROPERTIES SOURCES "${_ui_srcs}")
endif()
target_link_libraries(xenia-ui PUBLIC xenia-base)
if(WIN32)
  target_link_libraries(xenia-ui PRIVATE dwmapi dxgi winmm)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_libraries(xenia-ui PRIVATE xcb X11 X11-xcb fontconfig ${GTK3_LIBRARIES})
endif()
xe_target_defaults(xenia-ui)
