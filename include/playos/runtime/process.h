// PlayOS Runtime — process launching.
//
// Implements the "launch a game and know when it exits" contract from
// playos-spec: book/src/08-runtime-architecture/07-package-execution.md.
// The launched process runs in its own process group so it can be supervised
// and torn down as a unit.
#pragma once

#include <string>
#include <vector>

namespace PlayOS {
namespace Runtime {

struct LaunchResult {
    bool launched = false; // did the process start?
    int exitCode = -1;     // process exit code (valid only if launched)
};

// Launches an executable, waits for it to exit, and returns the result.
// Blocks until the child process terminates.
LaunchResult LaunchAndWait(const std::string& executable,
                           const std::vector<std::string>& args = {});

} // namespace Runtime
} // namespace PlayOS
