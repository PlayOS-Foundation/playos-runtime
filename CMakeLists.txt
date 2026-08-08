cmake_minimum_required(VERSION 3.10)
project(playos-runtime C)
# Protocol-only package after S2.5-T1 IPC unification.
# Canonical IPC C sources live in playos-refdistro/src/playos-init/ipc/

set(CMAKE_C_STANDARD 99)

# ── Installation ─────────────────────────────────────────────
install(
    DIRECTORY protocols/
    DESTINATION share/playos/protocols
    FILES_MATCHING PATTERN "*.xml"
)
