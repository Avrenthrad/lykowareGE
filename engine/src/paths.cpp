#include <lykoware/paths.hpp>

#include <SDL3/SDL_filesystem.h>

#include <string>
#include <string_view>

namespace lykoware {

std::filesystem::path executable_directory()
{
    // UTF-8, absolute, ending in a separator, cached by SDL; SDL owns it.
    const char* base = SDL_GetBasePath();
    if (base == nullptr || *base == '\0') {
        return {};
    }
    const std::string_view text{base};
    return std::filesystem::path{std::u8string(text.begin(), text.end())}.parent_path();
}

std::filesystem::path shader_directory()
{
    std::filesystem::path directory = executable_directory();
    if (directory.empty()) {
        return {};
    }
    return directory / "shaders";
}

}
