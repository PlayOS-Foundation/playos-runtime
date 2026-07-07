// playos-run — minimal CLI that launches an executable and waits for it,
// returning the child's exit code. Useful on its own and as a reference for
// how the shell asks the runtime to launch a game.
#include "playos/runtime/process.h"

#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: playos-run <executable> [args...]\n");
        return 2;
    }

    const std::string executable = argv[1];
    std::vector<std::string> args;
    for (int i = 2; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    const auto result = PlayOS::Runtime::LaunchAndWait(executable, args);
    if (!result.launched) {
        std::fprintf(stderr, "playos-run: failed to launch '%s'\n",
                     executable.c_str());
        return 1;
    }

    std::printf("playos-run: '%s' exited with code %d\n",
                executable.c_str(), result.exitCode);
    return result.exitCode;
}
