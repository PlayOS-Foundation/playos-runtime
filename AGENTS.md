# AGENTS.md — playos-runtime

> **Implementation status:** 🟡 In Progress — Wayland protocol XML (`protocols/playos-v1.xml`) exists. IPC C source (`include/`, `src/`, `tests/`) not yet implemented. This AGENTS.md describes the **target** structure.

This repository contains the **internal runtime IPC layer** — the Unix domain socket protocol and Wayland protocol XML that connect `playos-init`, `playos-compositor`, `playos-shell`, and `playos-overlay`. Games do not use this directly; they go through `libplayos` (see `playos-platform-api`).

## Specification Reference

Before touching any file here, read:
- [`playos-spec/src/runtime-ipc.md`](https://github.com/PlayOS-Foundation/playos-spec/blob/main/src/runtime-ipc.md) — IPC framing, all message types, lifecycle fd protocol
- [`playos-spec/src/wayland-protocol.md`](https://github.com/PlayOS-Foundation/playos-spec/blob/main/src/wayland-protocol.md) — private Wayland protocol design and trust model
- [`playos-spec/src/security-model.md`](https://github.com/PlayOS-Foundation/playos-spec/blob/main/src/security-model.md) — socket permissions, trusted group

## Repository Layout

```
protocols/
└── playos-v1.xml       ← Private Wayland protocol XML (4 interfaces)

include/playos-runtime/
└── ipc.h               ← Message type definitions, framing constants

src/
├── ipc_client.c        ← Client-side connect / send / recv helpers
├── ipc_server.c        ← Server-side listen / dispatch helpers
└── lifecycle_fd.c      ← Lifecycle pipe fd helpers

tests/
└── ...

CMakeLists.txt
```

## IPC Protocol Rules — Critical

- **Socket path**: `/run/playos/control.sock`, mode `0660`, group `playos-trusted`.
- **Frame format**: 4-byte magic `PLOS` + 4-byte LE length + JSON body. Never change the magic or frame layout without a new ADR.
- **Every message** must carry `"v": 1` (protocol version field). Receivers must reject unknown versions.
- **Message type strings** are the stable ABI — do not rename them. Add new types; never remove old ones while any component still sends them.
- **Lifecycle fd**: single-byte events over a pipe fd (`PLAYOS_LIFECYCLE_FD` env var). Values are defined in `ipc.h`.

## Wayland Protocol Rules

- `protocols/playos-v1.xml` is consumed by `wayland-scanner` in `playos-compositor` and `playos-shell`. Do not rename interfaces or requests without bumping the version attribute and coordinating with both consumers.
- The four interfaces are: `playos_session_manager`, `playos_game_surface`, `playos_overlay_surface`, `playos_input_router`. Do not merge or split them without an ADR.
- Only `playos-trusted` group processes may bind `playos_session_manager`.

## Code Conventions

- C99, no external runtime dependencies beyond libc and libwayland-server (for the XML scanner output).
- All exported symbols prefixed `playos_ipc_` or `playos_lifecycle_`.
- IPC helpers are **not** thread-safe — callers must serialize. Document this on every public function.

## Build Commands

```sh
cmake -B build
cmake --build build
# Generate Wayland glue (done automatically by CMake):
wayland-scanner server-header protocols/playos-v1.xml gen/playos-v1-protocol.h
wayland-scanner private-code  protocols/playos-v1.xml gen/playos-v1-protocol.c
```

## What NOT to Do

- Do not put compositor rendering logic here — that belongs in `playos-compositor`.
- Do not add game-facing API here — that belongs in `playos-platform-api`.
- Do not change the IPC socket path or permissions without updating `playos-spec/src/security-model.md` and `playos-spec/src/runtime-ipc.md`.
- Do not introduce a dependency on wlroots from this repo — it must remain linkable by `libplayos`'s real backend without pulling in compositor internals.
