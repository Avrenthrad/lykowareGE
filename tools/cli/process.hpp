#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace lykoware::cli {
// Starts one child without a shell, inherits its streams, waits, and returns its
// exit code. The child alone changes working directory. Launch failure is 127;
// POSIX signal termination is 128 + signal. No engine/runtime dependency.
int run_process(const std::string& executable, const std::vector<std::string>& arguments,
                const std::filesystem::path& directory);

#ifdef _WIN32
std::string to_utf8(const wchar_t* text);
#endif
}
