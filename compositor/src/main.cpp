// PlayOS Compositor — entry point.
//
// Usage: playos-compositor [shell-command]
//        PLAYOS_SHELL_CMD=... playos-compositor
//
// The shell command is launched as a Wayland client after the compositor
// starts. If neither argument nor env var is provided, the compositor
// runs without a shell (useful for testing with manually-launched clients).

#include "playos/compositor/compositor.hpp"

#include <cstdlib>
#include <unistd.h>

int main(int argc, char* argv[]) {
    // Use WLR_DEBUG when PLAYOS_DEBUG is set (useful for diagnosing
    // black-screen-on-boot issues after disk install).
    const char* debug_env = getenv("PLAYOS_DEBUG");
    if (debug_env && debug_env[0] == '1') {
        wlr_log_init(WLR_DEBUG, nullptr);
    } else {
        wlr_log_init(WLR_INFO, nullptr);
    }

    const char* shell_cmd = nullptr;
    if (argc > 1) {
        shell_cmd = argv[1];
    } else {
        shell_cmd = getenv("PLAYOS_SHELL_CMD");
    }

    wlr_log(WLR_INFO, "PlayOS compositor starting (pid=%d)", getpid());
    wlr_log(WLR_INFO, "shell command: %s", shell_cmd ? shell_cmd : "(none)");

    PlayOS::Compositor compositor;
    if (!compositor.init()) {
        wlr_log(WLR_ERROR, "compositor init() failed — check seatd, GPU (/dev/dri/card*), "
                "and permissions");
        return 1;
    }

    compositor.run(shell_cmd);
    wlr_log(WLR_INFO, "compositor exiting normally");
    return 0;
}
