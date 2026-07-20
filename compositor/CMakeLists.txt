# PlayOS Compositor (Linux only) — C++ RAII compositor on wlroots.
#
# Requires C++17, wlroots, wayland-server, and xkbcommon.
# Built only when PLAYOS_BUILD_COMPOSITOR=ON and on Linux.
# See BRINGUP.md for required packages.

find_package(PkgConfig REQUIRED)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

pkg_check_modules(WAYLAND_SERVER REQUIRED wayland-server)
pkg_check_modules(XKBCOMMON REQUIRED xkbcommon)

# wlroots is versioned in its pkg-config name (wlroots-0.19, wlroots-0.18, ...).
# Try known versions, newest first, then a bare fallback.
pkg_check_modules(WLROOTS wlroots-0.19)
if(NOT WLROOTS_FOUND)
    pkg_check_modules(WLROOTS wlroots-0.18)
endif()
if(NOT WLROOTS_FOUND)
    pkg_check_modules(WLROOTS REQUIRED wlroots)
endif()

message(STATUS "PlayOS compositor: using ${WLROOTS_MODULE_NAME} "
               "(version ${WLROOTS_VERSION})")

# Generate xdg-shell protocol headers from the XML shipped with
# wayland-protocols (needed by wlroots 0.19 on Alpine where only enum
# headers are pre-installed).
pkg_check_modules(WAYLAND_PROTOCOLS wayland-protocols)
if(NOT WAYLAND_PROTOCOLS_FOUND)
    # Alpine doesn't ship a .pc for wayland-protocols — find the XML manually.
    execute_process(
        COMMAND ${WAYLAND_SCANNER} --version
        OUTPUT_VARIABLE _ws_ver OUTPUT_STRIP_TRAILING_WHITESPACE)
    # The protocol XML lives under the wayland-scanner data dir.
    get_filename_component(_ws_bindir ${WAYLAND_SCANNER} DIRECTORY)
    set(WAYLAND_PROTOCOLS_DATADIR "${_ws_bindir}/../share/wayland-protocols")
else()
    set(WAYLAND_PROTOCOLS_DATADIR "${WAYLAND_PROTOCOLS_PREFIX}/share/wayland-protocols")
endif()

set(XDG_SHELL_XML "${WAYLAND_PROTOCOLS_DATADIR}/stable/xdg-shell/xdg-shell.xml")
set(XDG_SHELL_PROTO_H "${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.h")
set(XDG_SHELL_PROTO_C "${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.c")

add_custom_command(
    OUTPUT "${XDG_SHELL_PROTO_H}" "${XDG_SHELL_PROTO_C}"
    COMMAND ${WAYLAND_SCANNER} server-header "${XDG_SHELL_XML}" "${XDG_SHELL_PROTO_H}"
    COMMAND ${WAYLAND_SCANNER} private-code "${XDG_SHELL_XML}" "${XDG_SHELL_PROTO_C}"
    DEPENDS "${XDG_SHELL_XML}"
    COMMENT "Generating xdg-shell protocol from ${XDG_SHELL_XML}"
)

add_executable(playos-compositor
    src/main.cpp
    src/compositor.cpp
    "${XDG_SHELL_PROTO_C}"
)

target_compile_features(playos-compositor PRIVATE cxx_std_17)
target_compile_definitions(playos-compositor PRIVATE WLR_USE_UNSTABLE)

target_include_directories(playos-compositor PRIVATE
    include
    ${CMAKE_CURRENT_BINARY_DIR}
    ${WLROOTS_INCLUDE_DIRS}
    ${WAYLAND_SERVER_INCLUDE_DIRS}
    ${XKBCOMMON_INCLUDE_DIRS})

target_link_libraries(playos-compositor PRIVATE
    ${WLROOTS_LIBRARIES}
    ${WAYLAND_SERVER_LIBRARIES}
    ${XKBCOMMON_LIBRARIES})

target_link_directories(playos-compositor PRIVATE
    ${WLROOTS_LIBRARY_DIRS}
    ${WAYLAND_SERVER_LIBRARY_DIRS}
    ${XKBCOMMON_LIBRARY_DIRS})
