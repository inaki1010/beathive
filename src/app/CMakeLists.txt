add_executable(beathive main.cpp)
target_link_libraries(beathive PRIVATE bh::ui bh_warnings)

include(GNUInstallDirs)
install(TARGETS beathive RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(UNIX AND NOT APPLE)
  install(FILES ${PROJECT_SOURCE_DIR}/packaging/beathive.desktop
          DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  install(FILES ${PROJECT_SOURCE_DIR}/packaging/beathive.svg
          DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()
