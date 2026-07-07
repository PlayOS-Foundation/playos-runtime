cmake_minimum_required(VERSION 3.20)
project(playos_runtime VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Runtime library: process launching and supervision.
add_library(playos-runtime
    src/process.cpp
)
add_library(PlayOS::Runtime ALIAS playos-runtime)

target_include_directories(playos-runtime PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

if(WIN32)
    target_sources(playos-runtime PRIVATE src/process_windows.cpp)
    target_compile_definitions(playos-runtime PRIVATE PLAYOS_RUNTIME_WINDOWS)
else()
    target_sources(playos-runtime PRIVATE src/process_posix.cpp)
    target_compile_definitions(playos-runtime PRIVATE PLAYOS_RUNTIME_POSIX)
endif()

# playos-run: a small CLI that launches an executable and waits for it.
add_executable(playos-run src/main.cpp)
target_link_libraries(playos-run PRIVATE playos-runtime)

# The wlroots/TinyWL compositor is Linux-only (ADR-0002) and off by default so
# Windows and CI builds of the runtime library are unaffected. Enable with
# -DPLAYOS_BUILD_COMPOSITOR=ON on a Linux host with wlroots installed.
option(PLAYOS_BUILD_COMPOSITOR "Build the Linux wlroots compositor" OFF)
if(PLAYOS_BUILD_COMPOSITOR AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_subdirectory(compositor)
endif()
