# AGENTS.md — playos-runtime

> **Implementation status:** 🟡 Protocol-only — `protocols/playos-v1.xml` defines 4 Wayland interfaces. The runtime IPC C sources previously planned here now live in `playos-refdistro/src/playos-init/ipc/`. This repository is the canonical source for the protocol XML; it contains no C implementation code.

This repository defines the **internal runtime protocols** — the Wayland protocol XML and IPC message types that connect `playos-init`, `playos-compositor`, `playos-shell`, and `playos-overlay`. Games do not use this directly; they go through `libplayos` (see `playos-platform-api`).

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
└── (empty — IPC headers live in playos-refdistro/src/playos-init/ipc/)

CMakeLists.txt           ← wayland-scanner glue generation only
```

> **Note:** IPC C sources (`ipc_client.c`, `ipc_server.c`, `lifecycle_fd.c`) and headers moved to `playos-refdistro/src/playos-init/ipc/`. This repo is the canonical home for the protocol XML only.

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
