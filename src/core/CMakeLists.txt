find_package(SQLite3 REQUIRED)
# CMake >= 4.3 renamed the imported target.
if(TARGET SQLite3::SQLite3)
  set(BH_SQLITE_TARGET SQLite3::SQLite3)
else()
  set(BH_SQLITE_TARGET SQLite::SQLite3)
endif()
find_package(TagLib 2 REQUIRED)

find_package(nlohmann_json 3.11 QUIET)
if(NOT nlohmann_json_FOUND)
  include(FetchContent)
  FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
  FetchContent_MakeAvailable(nlohmann_json)
endif()

add_library(bh_core STATIC
  db/art_repo.cpp
  db/folder_repo.cpp
  db/playlist_repo.cpp
  db/query.cpp
  db/rules_json.cpp
  db/schema.cpp
  db/sqlite.cpp
  db/track_repo.cpp
  library/cover_finder.cpp
  library/scanner.cpp
  model/queue.cpp
  model/rules.cpp
  model/track.cpp
  tags/tag_io.cpp
  tags/tag_parse.cpp
  util/text.cpp
)
add_library(bh::core ALIAS bh_core)
target_include_directories(bh_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(bh_core PUBLIC cxx_std_23)
target_link_libraries(bh_core PRIVATE bh_warnings)
target_link_libraries(bh_core PUBLIC ${BH_SQLITE_TARGET} TagLib::tag nlohmann_json::nlohmann_json)

# ---- playback: FFmpeg decoding + miniaudio output (kept separate from bh_core) ----
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libswresample libavutil)

include(FetchContent)
FetchContent_Declare(miniaudio
  GIT_REPOSITORY https://github.com/mackron/miniaudio.git
  GIT_TAG 0.11.22
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR do-not-build)  # single header: download only
FetchContent_MakeAvailable(miniaudio)

find_package(Threads REQUIRED)
add_library(bh_playback STATIC
  playback/controller.cpp
  playback/engine.cpp
  playback/ffmpeg_decoder.cpp
  playback/miniaudio_output.cpp
)
add_library(bh::playback ALIAS bh_playback)
target_include_directories(bh_playback PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_include_directories(bh_playback PRIVATE ${miniaudio_SOURCE_DIR})
target_link_libraries(bh_playback PUBLIC bh_core PRIVATE bh_warnings PkgConfig::FFMPEG Threads::Threads ${CMAKE_DL_LIBS})
