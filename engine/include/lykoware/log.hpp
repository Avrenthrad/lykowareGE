#pragma once

#include <string_view>

namespace lykoware {
enum class LogLevel { info, warning, error };

// Writes a complete tagged line to stderr. No global initialization required.
void log(LogLevel level, std::string_view message);
}
