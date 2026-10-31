cmake_minimum_required(VERSION 3.25)
project(beathive VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BH_BUILD_TESTS "Build unit tests" ON)

# Strict warnings for our code only (not FetchContent dependencies).
add_library(bh_warnings INTERFACE)
if(MSVC)
  target_compile_options(bh_warnings INTERFACE /W4 /permissive- /utf-8)
else()
  target_compile_options(bh_warnings INTERFACE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

add_subdirectory(src/core)

option(BH_BUILD_UI "Build the Qt application" ON)
if(BH_BUILD_UI)
  add_subdirectory(src/ui)
  add_subdirectory(src/app)
endif()
add_subdirectory(tools)

if(BH_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
