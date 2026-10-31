add_executable(bh_scan bh_scan.cpp)
target_link_libraries(bh_scan PRIVATE bh::core bh_warnings)

add_executable(bh_play bh_play.cpp)
target_link_libraries(bh_play PRIVATE bh::playback bh_warnings)
