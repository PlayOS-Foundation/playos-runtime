cmake_minimum_required(VERSION 3.10)
project(playos-runtime C)
# Protocol-only package after S2.5-T1 IPC unification.
# Canonical IPC C sources live in playos-refdistro/src/playos-init/ipc/

set(CMAKE_C_STANDARD 99)

# ── Path to IPC sources (from playos-refdistro) ─────────────
set(PLAYOS_IPC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../playos-refdistro/src/playos-init/ipc"
    CACHE PATH "Path to IPC framing sources (ipc.h, ipc_framing.c, ipc_client.c)")

# ── Trusted Control IPC Client Library ──────────────────────
# Only linked by trusted system components (shell, overlay).
# Never exposed to games.

add_library(playos-trusted STATIC
    src/trusted_control.c
    ${PLAYOS_IPC_DIR}/ipc_framing.c
    ${PLAYOS_IPC_DIR}/ipc_client.c
)

target_include_directories(playos-trusted PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PLAYOS_IPC_DIR}
)

target_compile_definitions(playos-trusted PRIVATE
    _POSIX_C_SOURCE=200809L
    _DEFAULT_SOURCE
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(playos-trusted PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# ── Installation ─────────────────────────────────────────────
install(
    DIRECTORY protocols/
    DESTINATION share/playos/protocols
    FILES_MATCHING PATTERN "*.xml"
)

install(
    DIRECTORY include/playos-runtime/
    DESTINATION include/playos-runtime
    FILES_MATCHING PATTERN "*.h"
)

install(TARGETS playos-trusted
    ARCHIVE DESTINATION lib
)

# ── Tests ─────────────────────────────────────────────────────

option(BUILD_TESTS "Build host tests" ON)

if(BUILD_TESTS)
    enable_testing()

    # S12-T6: trusted control-socket ownership + peer policy.
    add_executable(test-trusted-control
        tests/test_trusted_control.c
        ${PLAYOS_IPC_DIR}/ipc_server.c
        ${PLAYOS_IPC_DIR}/ipc_framing.c
    )
    target_include_directories(test-trusted-control PRIVATE ${PLAYOS_IPC_DIR})
    target_compile_definitions(test-trusted-control PRIVATE _GNU_SOURCE)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(test-trusted-control PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME trusted-control COMMAND test-trusted-control)
endif()
