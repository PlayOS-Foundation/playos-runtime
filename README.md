# playos-runtime

Reference runtime for PlayOS — responsible for application lifecycle, package
execution, runtime services, and operating system integration.

Implements contracts from
[`playos-spec`](https://github.com/PlayOS-Foundation/playos-spec) (Part VIII,
Runtime Architecture). Platform behavior is specified there first.

## Status

Early scaffold. Currently provides the **package-execution** primitive needed
by the vertical slice: launch an executable and wait for it to exit.

- **`playos-runtime`** (library) — `PlayOS::Runtime::LaunchAndWait(...)`,
  launching a child in its own process group (CreateProcess on Windows,
  fork/exec on POSIX).
- **`playos-run`** (CLI) — `playos-run <executable> [args...]` launches and
  waits, returning the child's exit code. The shell uses the same library to
  launch games and detect their exit (the "return to shell" loop).

> The Linux compositor (wlroots/TinyWL, per ADR-0002) will also live in this
> repository as it is brought up on hardware. It is Linux-only and is not part
> of the Windows-first scaffold.

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
