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

int main(int argc, char* argv[]) {
    wlr_log_init(WLR_INFO, nullptr);

    const char* shell_cmd = nullptr;
    if (argc > 1) {
        shell_cmd = argv[1];
    } else {
        shell_cmd = getenv("PLAYOS_SHELL_CMD");
    }

    PlayOS::Compositor compositor;
    if (!compositor.init()) {
        return 1;
    }

    compositor.run(shell_cmd);
    return 0;
}
