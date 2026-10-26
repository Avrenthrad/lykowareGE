#include <lykoware/log.hpp>

#include <iostream>
#include <syncstream>

namespace lykoware {
void log(LogLevel level, std::string_view message)
{
    std::string_view label = "INFO";
    switch (level) {
    case LogLevel::info: break;
    case LogLevel::warning: label = "WARN"; break;
    case LogLevel::error: label = "ERROR"; break;
    }

    std::osyncstream{std::clog} << '[' << label << "] " << message << '\n';
}
}
