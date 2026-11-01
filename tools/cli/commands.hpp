#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace lykoware::cli {
// Exit codes for this process. Child processes may return other codes.
inline constexpr int exit_ok = 0;
inline constexpr int exit_usage = 2;
inline constexpr int exit_unsupported = 3;
inline constexpr int exit_not_launched = 127;

// One fixed dispatcher for lyko subcommands.
//
// CLI and a future scripting entry point call these same handlers. The table
// is compiled into this tool. It is not a serializable editor command bus,
// not a plugin registry, and not an engine API. Engine code does not include
// this header.
//
// Ownership: `program` and `arguments` are non-owning views of the caller's
// text. dispatch does not copy them into storage that outlives the call, and
// handlers must not retain pointers, references, or spans after they return.
// A handler that starts a child copies the argument bytes the OS requires and
// releases its process handles before returning.
//
// Lifetime: dispatch is synchronous. It returns only after the selected
// handler finishes, including a child process it waited for. There is no
// queued work, callback, or registered command left behind. The caller's
// argument storage must remain valid for the entire call.
//
// Threading: call dispatch from one thread. Handlers write to the standard
// streams and create processes without internal locking. Concurrent dispatch
// is not supported. No handler starts a background thread.
//
// Failure: expected rejects return an exit code and do not throw.
// exit_usage (2) is an unknown command or a bad option, including trace
// without a valid --frames value. exit_unsupported (3) is screenshot or
// run --scene: the message contains "unsupported: no renderer" and no
// renderer is started. exit_not_launched (127) means the child executable
// could not be started. trace --frames N launches lykoware_sandbox, records
// that many CPU-loop iterations, and writes a Chrome trace; the sandbox
// status is returned. run with no scene launches lykoware_sandbox and
// returns that process's status. Other codes are the child's status.
// Unexpected std::exceptions are reported on stderr and returned as
// exit_usage.
int dispatch(std::string_view program, const std::vector<std::string>& arguments);
}
