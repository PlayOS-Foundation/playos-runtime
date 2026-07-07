# Copilot instructions — playos-runtime

Reference **PlayOS Runtime**: application lifecycle, package execution, runtime
services, OS integration, and (on Linux) the wlroots/TinyWL compositor. Source
of truth is [`playos-spec`](https://github.com/PlayOS-Foundation/playos-spec)
(Part VIII). Also read `AGENTS.md`.

## Rules for changes here

1. **Spec first.** Behavior that affects the platform is specified in
   `playos-spec` before implementation.
2. **Portable core, platform backends.** Cross-platform logic in shared files;
   OS-specific code in `*_windows.cpp` / `*_posix.cpp`, selected by CMake. No
   OS `#ifdef` in public headers.
3. **Supervise as a unit.** Launched apps run in their own process group so
   they can be tracked/torn down cleanly; always detect exit so the shell
   regains the foreground.
4. **Compositor is Linux-only.** It lives under `compositor/`, behind
   `-DPLAYOS_BUILD_COMPOSITOR=ON`; keep it isolated so Windows builds are
   unaffected. It targets wlroots 0.19 (see `compositor/BRINGUP.md`).
5. **Keep it building** on Windows (MSVC) and Linux (GCC/Clang).

## Where things go

- Public headers: `include/playos/runtime/`
- Launcher: `src/process*.cpp`, CLI in `src/main.cpp`
- Compositor: `compositor/`
