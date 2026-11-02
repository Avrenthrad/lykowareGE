#include "process.hpp"

#include <lykoware/version.hpp>

#include <exception>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void usage()
{
    std::cout << "lyko " << lykoware::version << " — Lykoware native tools\n"
                 "Usage:\n"
                 "  lyko build [--source DIR] [--configure-preset NAME] [--preset NAME]\n"
                 "             [--cmake EXECUTABLE]\n"
                 "  lyko test  [--source DIR] [--preset NAME] [--ctest EXECUTABLE]\n"
                 "  lyko --help | --version\n"
                 "Tools default to cmake/ctest on PATH; source defaults to the current directory.\n"
                 "Windows defaults: configure windows, build/test windows-debug.\n"
                 "Linux defaults: configure/build/test linux-debug.\n"
                 "run, screenshot and trace are not implemented yet.\n";
}

std::string tool_path(const std::string& value)
{
    const std::filesystem::path path{std::u8string(value.begin(), value.end())};
    if (!path.has_parent_path()) {
        return value;
    }
    const auto encoded = std::filesystem::absolute(path).u8string();
    return {encoded.begin(), encoded.end()};
}

int run(const std::vector<std::string>& args)
{
    if (args.empty() || (args.size() == 1 && args[0] == "--help")) {
        usage();
        return 0;
    }
    if (args.size() == 1 && args[0] == "--version") {
        std::cout << "lyko " << lykoware::version << '\n';
        return 0;
    }
    const auto& command = args[0];
    if (command == "run" || command == "screenshot" || command == "trace") {
        std::cerr << "lyko: " << command << " is not implemented; see docs/STATUS.md.\n";
        return 3;
    }
    if (command != "build" && command != "test") {
        throw std::invalid_argument("Unknown command: " + command + ". Use --help.");
    }
    if (args.size() == 2 && args[1] == "--help") {
        usage();
        return 0;
    }

    std::filesystem::path source = std::filesystem::current_path();
#ifdef _WIN32
    std::string configure_preset = "windows";
    std::string preset = "windows-debug";
#else
    std::string configure_preset = "linux-debug";
    std::string preset = "linux-debug";
#endif
    std::string executable = command == "build" ? "cmake" : "ctest";
    std::set<std::string> seen;
    for (std::size_t index = 1; index < args.size(); ++index) {
        const auto& option = args[index];
        const bool valid = option == "--source" || option == "--preset"
            || (command == "build" && (option == "--configure-preset" || option == "--cmake"))
            || (command == "test" && option == "--ctest");
        if (!valid) {
            throw std::invalid_argument("Unknown option for " + command + ": " + option);
        }
        if (!seen.insert(option).second) {
            throw std::invalid_argument("Duplicate option: " + option);
        }
        if (index + 1 >= args.size() || args[index + 1].empty()
            || args[index + 1].starts_with("--")) {
            throw std::invalid_argument("Missing value for " + option);
        }
        const auto& value = args[++index];
        if (option == "--source") {
            source = std::filesystem::path{std::u8string(value.begin(), value.end())};
        } else if (option == "--preset" || option == "--configure-preset") {
            if (value.starts_with('-')) {
                throw std::invalid_argument("Preset names cannot start with '-'.");
            }
            (option == "--preset" ? preset : configure_preset) = value;
        } else {
            executable = tool_path(value);
        }
    }
    source = std::filesystem::canonical(source);
    if (!std::filesystem::is_regular_file(source / "CMakePresets.json")) {
        throw std::invalid_argument("Source directory must contain CMakePresets.json.");
    }
    if (command == "build") {
        const int configured = lykoware::cli::run_process(
            executable, {"--preset", configure_preset}, source);
        if (configured != 0) {
            return configured;
        }
        return lykoware::cli::run_process(executable, {"--build", "--preset", preset}, source);
    }
    return lykoware::cli::run_process(
        executable, {"--preset", preset, "--output-on-failure", "--no-tests=error"}, source);
}

int guarded_run(const std::vector<std::string>& args)
{
    try {
        return run(args);
    } catch (const std::exception& error) {
        std::cerr << "lyko: " << error.what() << '\n';
        return 2;
    }
}
}

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[])
{
    try {
        std::vector<std::string> args;
        for (int index = 1; index < argc; ++index) {
            args.push_back(lykoware::cli::to_utf8(argv[index]));
        }
        return guarded_run(args);
    } catch (const std::exception& error) {
        std::cerr << "lyko: " << error.what() << '\n';
        return 2;
    }
}
#else
int main(int argc, char* argv[])
{
    return guarded_run({argv + 1, argv + argc});
}
#endif
