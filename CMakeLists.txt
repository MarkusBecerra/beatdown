cmake_minimum_required(VERSION 3.25)

# Self-contained binary on Windows: static libs + static CRT. Must precede project().
if(CMAKE_HOST_WIN32 AND NOT DEFINED VCPKG_TARGET_TRIPLET)
  set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "")
endif()

project(beatdown VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  add_compile_options(/W4 /utf-8 /EHsc)
else()
  add_compile_options(-Wall -Wextra)
endif()

find_package(SndFile CONFIG REQUIRED)
find_package(mp3lame CONFIG REQUIRED)
find_package(CLI11 CONFIG REQUIRED)
find_package(Threads REQUIRED)

add_library(beatdown_core STATIC
  src/core/version.cpp
  src/core/tags.cpp
  src/core/unicode.cpp
)
target_include_directories(beatdown_core PUBLIC src)
target_link_libraries(beatdown_core PUBLIC SndFile::sndfile mp3lame::mp3lame Threads::Threads)
target_compile_definitions(beatdown_core PUBLIC BEATDOWN_VERSION="${PROJECT_VERSION}")
if(WIN32)
  target_compile_definitions(beatdown_core PUBLIC
    ENABLE_SNDFILE_WINDOWS_PROTOTYPES=1 NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
  target_sources(beatdown_core PRIVATE src/core/platform/win.cpp)
else()
  target_sources(beatdown_core PRIVATE src/core/platform/posix.cpp)
endif()

add_executable(beatdown src/cli/main.cpp)
target_link_libraries(beatdown PRIVATE beatdown_core CLI11::CLI11)

option(BEATDOWN_BUILD_TESTS "Build the test suite" ON)
if(BEATDOWN_BUILD_TESTS)
  find_package(Catch2 3 CONFIG REQUIRED)
  enable_testing()
  add_executable(beatdown_tests
    tests/test_version.cpp
    tests/test_tags.cpp
    tests/test_unicode.cpp
  )
  target_link_libraries(beatdown_tests PRIVATE beatdown_core Catch2::Catch2WithMain)
  target_compile_definitions(beatdown_tests PRIVATE BEATDOWN_BIN="$<TARGET_FILE:beatdown>")
  add_dependencies(beatdown_tests beatdown)
  include(Catch)
  catch_discover_tests(beatdown_tests)
endif()
