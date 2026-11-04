#pragma once

#include <filesystem>

namespace lykoware {

// Directory that holds the running executable (SDL_GetBasePath). Programs
// find the files deployed beside them from here, not from the current
// working directory, so they work wherever they are launched from.
// Returns an empty path when the platform cannot report it. Safe to call
// from any thread and before any window exists.
[[nodiscard]] std::filesystem::path executable_directory();

// Where a program's build-time compiled SPIR-V is deployed:
// executable_directory() / "shaders" (ADR 0016). Empty when
// executable_directory() is.
[[nodiscard]] std::filesystem::path shader_directory();

}
