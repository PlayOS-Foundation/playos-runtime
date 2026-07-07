// Shared (platform-agnostic) part of process launching. The actual launch is
// implemented per platform in process_windows.cpp / process_posix.cpp.
#include "playos/runtime/process.h"

// This translation unit intentionally contains no platform code. It exists so
// the library always has at least one common object and a place for future
// cross-platform helpers (argument quoting, logging, supervision policy).
namespace PlayOS {
namespace Runtime {

// (No shared helpers yet.)

} // namespace Runtime
} // namespace PlayOS
