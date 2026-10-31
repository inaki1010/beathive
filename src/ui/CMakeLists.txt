find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_library(bh_ui STATIC
  app_services.cpp
  controls.cpp
  cover_cache.cpp
  filter_browser.cpp
  glyphs.cpp
  library_dialog.cpp
  library_service.cpp
  main_window.cpp
  nav_tree.cpp
  now_playing_panel.cpp
  playback_bridge.cpp
  queue_model.cpp
  theme.cpp
  track_cache.cpp
  track_delegate.cpp
  track_table_model.cpp
  transport_bar.cpp
)
add_library(bh::ui ALIAS bh_ui)
set_target_properties(bh_ui PROPERTIES AUTOMOC ON)
target_include_directories(bh_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(bh_ui PUBLIC bh::playback Qt6::Widgets PRIVATE bh_warnings)
