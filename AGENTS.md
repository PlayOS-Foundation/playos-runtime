# AGENTS.md — playos-runtime

Guidance for AI agents and contributors working in this repository.

## What this repository is

The reference **PlayOS Runtime**: application lifecycle, package execution,
runtime services, OS integration, and (on Linux) the wlroots/TinyWL
compositor. It implements Part VIII of `playos-spec`; the specification is the
source of truth.

## Golden rules

1. **Spec first.** Behavior that affects the platform is specified in
   `playos-spec` before it is implemented here.
2. **Portable core, platform backends.** Cross-platform logic stays in shared
   files; OS-specific code goes in `*_windows.cpp` / `*_posix.cpp`, selected
   by CMake. No OS `#ifdef`s in public headers.
3. **Supervise as a unit.** Launched applications run in their own process
   group so they can be tracked and torn down cleanly (see the
   package-execution chapter).
4. **Return to the shell.** The runtime must always be able to detect a
   launched application's exit so the shell regains the foreground.

## Layout

| Path | Purpose |
|---|---|
| `include/playos/runtime/` | Public runtime headers |
| `src/process*.cpp` | Process launching (shared + per-OS) |
| `src/main.cpp` | `playos-run` CLI |

## Compositor (Linux, future)

The wlroots/TinyWL-based compositor (ADR-0002) belongs here and is Linux-only.
It is brought up on hardware (ROG Ally) and is not part of the Windows-first
scaffold. Keep it isolated behind CMake so Windows builds remain unaffected.

## Build

```sh
cmake -B build
cmake --build build
```
