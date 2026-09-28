cmake_minimum_required(VERSION 3.22)
project(opentax_android LANGUAGES C CXX)

# The tax engine is the same code the desktop app and CLI use: pull in the repository's
# top-level CMakeLists (engine only) and link the JNI bridge against it.
set(OPENTAX_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../..)
set(OPENTAX_BUILD_GUI OFF CACHE BOOL "" FORCE)
set(OPENTAX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${OPENTAX_ROOT} opentax EXCLUDE_FROM_ALL)

add_library(opentax_jni SHARED bridge.cpp)
target_link_libraries(opentax_jni PRIVATE opentax_core)
target_compile_options(opentax_jni PRIVATE -Wall -Wextra -Wpedantic)
# 16 KB page alignment, required for apps targeting Android 15+ devices.
target_link_options(opentax_jni PRIVATE "-Wl,-z,max-page-size=16384")
