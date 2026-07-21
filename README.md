# playos-runtime

Reference runtime for PlayOS — responsible for application lifecycle, package
execution, runtime services, and operating system integration.

Implements contracts from
[`playos-spec`](https://github.com/PlayOS-Foundation/playos-spec) (Part VIII,
Runtime Architecture). Platform behavior is specified there first.

## Status

**v0.3 — Stage 2 runtime integration complete.**

### playos-run (CLI)
`playos-run <executable> [args...]` launches and waits, returning the child's
exit code.

### playos-compositor (wlroots 0.19, C++17 RAII)

- DRM/KMS display bring-up, EGL/GLES2 rendering
- 4-layer scene tree per compositor model (§4): background (shell), game,
  overlay, system
- Explicit surface roles via `playos_shell_v1` and `playos_game_v1` protocols
- Runtime IPC via `playos_compositor_control_v1` (activate shell, game
  closed/shell closed events)
- Home-button interception (ROG Ally Armoury button → return to shell)
- Pointer focus tracking with scene-graph hit-testing (`wlr_scene_node_at`)
- Touch input routing
- Shell crash recovery (SIGCHLD handler, auto-respawn)
- Keyboard input routing through `wlr_seat`

### Known limitations (v0.3)

- No audio (PipeWire not wired yet).
- No suspend/resume.
- Overlay and system layers are reserved — protocols not yet implemented.
- See `compositor/BRINGUP.md` for detailed build & run instructions.

## Layout

```text
include/playos/runtime/process.h   Public launch API
src/process.cpp                    Shared (platform-agnostic) part
src/process_windows.cpp            CreateProcess implementation
src/process_posix.cpp              fork/exec/waitpid implementation
src/main.cpp                       playos-run CLI

compositor/
  include/playos/compositor/
    compositor.hpp                 PlayOS::Compositor class (C++17 RAII)
  src/
    compositor.cpp                 Implementation
    main.cpp                       Entry point
  protocol/                        Custom Wayland protocol XMLs
    playos-shell-v1.xml
    playos-game-v1.xml
    playos-compositor-control-v1.xml
  BRINGUP.md                       Build & run guide
```

## Building

Requires CMake >= 3.20, a C++17 compiler, and wlroots 0.19.

```sh
cmake -B build -G Ninja -DPLAYOS_BUILD_COMPOSITOR=ON
cmake --build build
```

Alternatively, build just the library and CLI (no compositor):

```sh
cmake -B build
cmake --build build
```

## License

Code will be released under an OSI-approved license (MIT/Apache-2.0); the
specification in `playos-spec` is CC-BY-4.0.
