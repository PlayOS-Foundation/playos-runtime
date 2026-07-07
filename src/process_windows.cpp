// Windows implementation of process launching using CreateProcess.
#include "playos/runtime/process.h"

#include <windows.h>

#include <string>

namespace PlayOS {
namespace Runtime {
namespace {

// Quotes an argument for the Windows command line if it contains spaces.
std::string quote(const std::string& arg) {
    if (arg.find(' ') == std::string::npos && !arg.empty()) {
        return arg;
    }
    return "\"" + arg + "\"";
}

} // namespace

LaunchResult LaunchAndWait(const std::string& executable,
                           const std::vector<std::string>& args) {
    LaunchResult result;

    // Build a mutable command line: "exe" arg1 arg2 ...
    std::string commandLine = quote(executable);
    for (const auto& arg : args) {
        commandLine += " ";
        commandLine += quote(arg);
    }

    STARTUPINFOA startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};

    // CREATE_NEW_PROCESS_GROUP so the child (and its children) can be
    // signalled/terminated as a unit, mirroring the POSIX process-group model.
    const BOOL ok = CreateProcessA(
        /*lpApplicationName*/ nullptr,
        /*lpCommandLine*/ commandLine.data(),
        /*lpProcessAttributes*/ nullptr,
        /*lpThreadAttributes*/ nullptr,
        /*bInheritHandles*/ FALSE,
        /*dwCreationFlags*/ CREATE_NEW_PROCESS_GROUP,
        /*lpEnvironment*/ nullptr,
        /*lpCurrentDirectory*/ nullptr,
        &startupInfo,
        &processInfo);

    if (!ok) {
        return result; // launched = false
    }

    result.launched = true;

    WaitForSingleObject(processInfo.hProcess, INFINITE);

    DWORD exitCode = 0;
    if (GetExitCodeProcess(processInfo.hProcess, &exitCode)) {
        result.exitCode = static_cast<int>(exitCode);
    }

    CloseHandle(processInfo.hProcess);
    CloseHandle(processInfo.hThread);
    return result;
}

} // namespace Runtime
} // namespace PlayOS
