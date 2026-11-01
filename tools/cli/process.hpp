#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace lykoware::cli {
// Starts one child without a shell and waits for it. Not an engine API.
// The Windows test fixture also calls to_utf8. No engine target includes this.
//
// Ownership: the caller owns `executable`, `arguments`, and `directory`.
// Nothing in those objects is retained after return. The operating system
// receives copies of the argument text. Standard streams are inherited, not
// captured or owned here. Windows process and thread handles are owned by a
// local RAII object and closed before return.
//
// Lifetime: the call blocks until the child exits or the wait fails. Handles
// owned here are closed before return or throw. A successful POSIX waitpid
// has reaped the child. The caller may destroy the arguments immediately
// after return.
//
// Threading: not synchronized. Call it from the one CLI thread. Do not call
// it concurrently: process creation and the error stream are process-wide.
// The child inherits the environment as it is at the moment of launch.
//
// Failure: returns 127 when the executable cannot be launched, and 128 plus
// the signal number when a POSIX child is killed by a signal. A Windows
// status that does not fit in signed int returns 1. Any other child status
// is returned unchanged. If waiting or reading the status fails, throws
// std::runtime_error. A child that was started but not successfully waited
// on may still be running; launch failure starts no child. Arguments are
// never interpreted by a shell.
int run_process(const std::string& executable, const std::vector<std::string>& arguments,
                const std::filesystem::path& directory);

#ifdef _WIN32
// Copies one null-terminated UTF-16 argument into a new UTF-8 string.
// Ownership: the result owns its bytes; `text` is not retained.
// Lifetime: no static buffer. Threading: safe on distinct inputs; uses no
// shared mutable state. Failure: throws std::runtime_error on invalid text.
std::string to_utf8(const wchar_t* text);
#endif
}
