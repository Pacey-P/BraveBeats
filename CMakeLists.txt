cmake_minimum_required(VERSION 3.10)
project(bravebeats CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# The generator is header only; these are the two include roots it needs
add_library(bravebeats_core INTERFACE)
target_include_directories(bravebeats_core INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/synthdrums606/include)

add_executable(bravebeats src/main.cpp)
target_link_libraries(bravebeats PRIVATE bravebeats_core)

if(MSVC)
  target_compile_options(bravebeats PRIVATE /W4)
else()
  target_compile_options(bravebeats PRIVATE -Wall -Wextra)
endif()

enable_testing()
add_executable(bravebeats_tests tests/tests.cpp)
target_link_libraries(bravebeats_tests PRIVATE bravebeats_core)
if(NOT MSVC)
  target_compile_options(bravebeats_tests PRIVATE -Wall -Wextra)
endif()
add_test(NAME bravebeats_tests COMMAND bravebeats_tests)
