# PlayOS Compositor (Linux only) — Stage 1 skeleton.
#
# Built only when PLAYOS_BUILD_COMPOSITOR=ON and on Linux. Requires wlroots,
# wayland-server, and xkbcommon via pkg-config. See BRINGUP.md for packages.

find_package(PkgConfig REQUIRED)

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

add_executable(playos-compositor playos-compositor.c)

target_compile_definitions(playos-compositor PRIVATE WLR_USE_UNSTABLE)

target_include_directories(playos-compositor PRIVATE
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
