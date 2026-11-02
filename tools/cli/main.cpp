#include "commands.hpp"

#ifdef _WIN32
#include "process.hpp"
#endif

#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[])
{
    try {
        const std::string program = argc > 0 ? lykoware::cli::to_utf8(argv[0]) : std::string{};
        std::vector<std::string> arguments;
        for (int index = 1; index < argc; ++index) {
            arguments.push_back(lykoware::cli::to_utf8(argv[index]));
        }
        return lykoware::cli::dispatch(program, arguments);
    } catch (const std::exception& error) {
        std::cerr << "lyko: " << error.what() << '\n';
        return lykoware::cli::exit_usage;
    }
}
#else
int main(int argc, char* argv[])
{
    const std::string_view program = argc > 0 ? std::string_view{argv[0]} : std::string_view{};
    std::vector<std::string> arguments;
    if (argc > 1) {
        arguments.assign(argv + 1, argv + argc);
    }
    return lykoware::cli::dispatch(program, arguments);
}
#endif
