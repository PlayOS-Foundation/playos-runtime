// POSIX implementation of process launching using fork/exec/waitpid.
// The child is placed in its own process group (setpgid) so it can be
// signalled and torn down as a unit, per the package-execution spec.
#include "playos/runtime/process.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <vector>

namespace PlayOS {
namespace Runtime {

LaunchResult LaunchAndWait(const std::string& executable,
                           const std::vector<std::string>& args) {
    LaunchResult result;

    // Build argv: executable, args..., nullptr.
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(executable.c_str()));
    for (const auto& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    const pid_t pid = fork();
    if (pid < 0) {
        return result; // launched = false
    }

    if (pid == 0) {
        // Child: own process group, then exec.
        setpgid(0, 0);
        execv(executable.c_str(), argv.data());
        _exit(127); // exec failed
    }

    // Parent: wait for the child.
    result.launched = true;
    int status = 0;
    if (waitpid(pid, &status, 0) == pid) {
        if (WIFEXITED(status)) {
            result.exitCode = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            result.exitCode = 128 + WTERMSIG(status);
        }
    }
    return result;
}

} // namespace Runtime
} // namespace PlayOS
