find_package(Catch2 3 CONFIG QUIET)
if(NOT Catch2_FOUND)
  include(FetchContent)
  FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1)
  FetchContent_MakeAvailable(Catch2)
  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
endif()

add_executable(bh_tests
  unit/ids_test.cpp
  unit/track_test.cpp
  unit/rules_test.cpp
  unit/queue_test.cpp
  unit/db_test.cpp
  unit/query_test.cpp
  unit/tags_test.cpp
  unit/scanner_test.cpp
  unit/cover_finder_test.cpp
  unit/ring_test.cpp
  unit/decoder_test.cpp
  unit/engine_test.cpp
  unit/controller_test.cpp
)
target_compile_definitions(bh_tests PRIVATE BH_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
target_link_libraries(bh_tests PRIVATE bh::core bh::playback Catch2::Catch2WithMain bh_warnings)

include(Catch)
catch_discover_tests(bh_tests)

if(TARGET bh_ui)
  find_package(Qt6 REQUIRED COMPONENTS Test)
  add_executable(bh_ui_tests
    ui/ui_main.cpp
    ui/models_test.cpp
    ui/main_window_test.cpp
    ui/widgets_test.cpp
  )
  target_compile_definitions(bh_ui_tests PRIVATE BH_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
  target_include_directories(bh_ui_tests PRIVATE unit)
  target_link_libraries(bh_ui_tests PRIVATE bh::ui Qt6::Test Catch2::Catch2 bh_warnings)
  catch_discover_tests(bh_ui_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TEST_PREFIX "ui: ")
endif()
