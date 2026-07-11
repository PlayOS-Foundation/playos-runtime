# playos-runtime

Reference runtime for PlayOS — responsible for application lifecycle, package
execution, runtime services, and operating system integration.

Implements contracts from
[`playos-spec`](https://github.com/PlayOS-Foundation/playos-spec) (Part VIII,
Runtime Architecture). Platform behavior is specified there first.

## Status

**v0.2 — ROG Ally confirmed working.**

- **`playos-runtime`** (library) — `PlayOS::Runtime::LaunchAndWait(...)`,
  launching a child in its own process group (CreateProcess on Windows,
  fork/exec on POSIX).
- **`playos-run`** (CLI) — `playos-run <executable> [args...]` launches and
  waits, returning the child's exit code.
- **`playos-compositor`** — wlroots 0.19/TinyWL-derived Wayland compositor.
  DRM/KMS display bring-up, EGL/GLES2 rendering, keyboard input routing,
  XDG toplevel configure. Built with `-DPLAYOS_BUILD_COMPOSITOR=ON` (Linux only).

### Known limitations (v0.2)

- **No pointer/touch input** — keyboard-only for now.
- **No audio** — compositor does not wire PipeWire yet.
- **No suspend/resume** — deferred to Stage 2.
- See `compositor/BRINGUP.md` for detailed build & run instructions.

## Layout

```text
include/playos/runtime/process.h   Public launch API
src/process.cpp                    Shared (platform-agnostic) part
src/process_windows.cpp            CreateProcess implementation
src/process_posix.cpp              fork/exec/waitpid implementation
src/main.cpp                       playos-run CLI
```

## Building

Requires CMake >= 3.20 and a C++17 compiler.

```sh
cmake -B build
cmake --build build
```

## License

Code will be released under an OSI-approved license (MIT/Apache-2.0); the
specification in `playos-spec` is CC-BY-4.0.
