#include "commands.hpp"
#include "scene_command.hpp"
#include "character_command.hpp"
#include "animation_command.hpp"

#include "lyko_sandbox_path.hpp"
#include "process.hpp"

#include <lykoware/trace.hpp>
#include <lykoware/version.hpp>

#include <charconv>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {
std::filesystem::path utf8_path(std::string_view text)
{
    return std::filesystem::path{std::u8string(text.begin(), text.end())};
}

std::string path_to_utf8(const std::filesystem::path& path)
{
    const auto encoded = path.u8string();
    return {encoded.begin(), encoded.end()};
}

bool is_file(const std::filesystem::path& path)
{
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
}

void usage()
{
    std::cout << "lyko " << lykoware::version << " — Lykoware native tools\n"
                 "Usage:\n"
                 "  lyko build [--source DIR] [--configure-preset NAME] [--preset NAME]\n"
                 "             [--cmake EXECUTABLE]\n"
                 "  lyko test  [--source DIR] [--preset NAME] [--ctest EXECUTABLE]\n"
                 "  lyko run [-- SANDBOX_OPTIONS]\n"
                 "  lyko animate-character --model FILE --graph FILE --inputs FILE [--target-model FILE]\n"
                 "  lyko inspect-character (--model FILE | --profile FILE) [--compare-model FILE] [--output FILE]\n"
                 "  lyko trace --frames N [--gpu] [--output FILE]\n"
                 "  lyko --help | --version\n"
                 "Tools default to cmake/ctest on PATH; source defaults to the current directory.\n"
                 "Windows defaults: configure windows, build/test windows-debug.\n"
                 "Linux defaults: configure/build/test linux-debug.\n"
                 "run launches lykoware_sandbox.\n"
                 "trace --frames N records that many sandbox CPU-loop iterations.\n"
                 "The default trace file is lykoware-cpu-trace.json in the current directory.\n"
                 "trace --gpu instead records N frames of the sandbox's Vulkan window: CPU zones\n"
                 "and, where the GPU has timestamps, GPU frame times (lykoware-gpu-trace.json).\n"
                 "It exits 3 when no window or Vulkan device is available.\n"
                 "run --scene FILE [--commands FILE | --script FILE [--record FILE]] [--ticks N]\n"
                 "    [--capture FILE] [--report FILE]\n"
                 "--script runs a sandboxed Lua script's on_tick(tick) each tick; its commands can\n"
                 "be written with --record and replayed with --commands.\n"
                 "screenshot --scene FILE --output FILE [--ticks N]\n"
                 "Scene options: --measure-frames N --warmup N --width N --height N.\n";
}

std::uint64_t parse_frames(std::string_view text)
{
    if (text.empty() || text.front() == '+' || text.front() == '-') {
        throw std::invalid_argument(
            "trace --frames must be an integer from 1 to "
            + std::to_string(lykoware::cpu_trace_frame_limit) + ".");
    }
    std::uint64_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()
        || value < 1 || value > lykoware::cpu_trace_frame_limit) {
        throw std::invalid_argument(
            "trace --frames must be an integer from 1 to "
            + std::to_string(lykoware::cpu_trace_frame_limit) + ".");
    }
    return value;
}

std::string tool_path(const std::string& value)
{
    const std::filesystem::path path = utf8_path(value);
    if (!path.has_parent_path()) {
        return value;
    }
    return path_to_utf8(std::filesystem::absolute(path));
}

std::filesystem::path this_executable(std::string_view program)
{
#ifdef _WIN32
    std::wstring buffer(32768, L'\0');
    const DWORD copied = GetModuleFileNameW(nullptr, buffer.data(),
                                            static_cast<DWORD>(buffer.size()));
    if (copied > 0 && copied < buffer.size()) {
        buffer.resize(copied);
        return std::filesystem::path{buffer};
    }
#else
    std::error_code error;
    const std::filesystem::path linked = std::filesystem::read_symlink("/proc/self/exe", error);
    if (!error) {
        return linked;
    }
#endif
    if (program.empty()) {
        return {};
    }
    return utf8_path(program);
}

std::filesystem::path sandbox_filename()
{
#ifdef _WIN32
    return "lykoware_sandbox.exe";
#else
    return "lykoware_sandbox";
#endif
}

std::filesystem::path find_sandbox(std::string_view program)
{
    const std::filesystem::path self = this_executable(program);
    if (!self.empty()) {
        const std::filesystem::path sibling = self.parent_path() / sandbox_filename();
        if (is_file(sibling)) {
            return sibling;
        }
    }
    const std::filesystem::path built = utf8_path(lykoware::cli::built_sandbox_path);
    if (is_file(built)) {
        return built;
    }
    return {};
}

using Handler = int (*)(std::string_view program, std::span<const std::string> arguments);

int build_or_test(bool build, std::span<const std::string> args)
{
    if (args.size() == 1 && args[0] == "--help") {
        usage();
        return lykoware::cli::exit_ok;
    }

    std::filesystem::path source = std::filesystem::current_path();
#ifdef _WIN32
    std::string configure_preset = "windows";
    std::string preset = "windows-debug";
#else
    std::string configure_preset = "linux-debug";
    std::string preset = "linux-debug";
#endif
    std::string executable = build ? "cmake" : "ctest";
    std::set<std::string> seen;
    for (std::size_t index = 0; index < args.size(); ++index) {
        const std::string& option = args[index];
        const bool valid = option == "--source" || option == "--preset"
            || (build && (option == "--configure-preset" || option == "--cmake"))
            || (!build && option == "--ctest");
        if (!valid) {
            throw std::invalid_argument("Unknown option for " + std::string{build ? "build" : "test"}
                                        + ": " + option);
        }
        if (!seen.insert(option).second) {
            throw std::invalid_argument("Duplicate option: " + option);
        }
        if (index + 1 >= args.size() || args[index + 1].empty()
            || args[index + 1].starts_with("--")) {
            throw std::invalid_argument("Missing value for " + option);
        }
        const std::string& value = args[++index];
        if (option == "--source") {
            source = utf8_path(value);
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
    if (!is_file(source / "CMakePresets.json")) {
        throw std::invalid_argument("Source directory must contain CMakePresets.json.");
    }
    if (build) {
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

int build_command(std::string_view, std::span<const std::string> arguments)
{
    return build_or_test(true, arguments);
}

int test_command(std::string_view, std::span<const std::string> arguments)
{
    return build_or_test(false, arguments);
}

int run_command(std::string_view program, std::span<const std::string> arguments)
{
    if (arguments.size() == 1 && arguments[0] == "--help") {
        usage();
        return lykoware::cli::exit_ok;
    }
    if(!arguments.empty()&&arguments.front()=="--"){
        const auto sandbox=find_sandbox(program);
        if(sandbox.empty()){std::cerr<<"lyko: cannot launch lykoware_sandbox (not found).\n";return lykoware::cli::exit_not_launched;}
        return lykoware::cli::run_process(path_to_utf8(sandbox),std::vector<std::string>(arguments.begin()+1,arguments.end()),std::filesystem::current_path());
    }
    for (const std::string& argument : arguments) {
        if (argument == "--scene" || argument.starts_with("--scene=")) {
            return lykoware::cli::scene_command(arguments, false);
        }
    }
    if (!arguments.empty()) {
        throw std::invalid_argument(
            "run accepts no options without a scene or explicit -- separator. Use --help.");
    }
    const std::filesystem::path sandbox = find_sandbox(program);
    if (sandbox.empty()) {
        std::cerr << "lyko: cannot launch lykoware_sandbox (not found).\n";
        return lykoware::cli::exit_not_launched;
    }
    return lykoware::cli::run_process(path_to_utf8(sandbox), {}, std::filesystem::current_path());
}

int screenshot_command(std::string_view, std::span<const std::string> arguments)
{
    return lykoware::cli::scene_command(arguments, true);
}

int trace_command(std::string_view program, std::span<const std::string> arguments)
{
    if (arguments.size() == 1 && arguments[0] == "--help") {
        usage();
        return lykoware::cli::exit_ok;
    }
    bool saw_frames = false;
    bool saw_output = false;
    bool gpu = false;
    std::uint64_t frames = 0;
    std::string output;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const std::string& option = arguments[index];
        if (option == "--gpu") {
            if (gpu) {
                throw std::invalid_argument("Duplicate option: --gpu");
            }
            gpu = true;
            continue;
        }
        if (option != "--frames" && option != "--output") {
            throw std::invalid_argument("Unknown option for trace: " + option);
        }
        if ((option == "--frames" && saw_frames) || (option == "--output" && saw_output)) {
            throw std::invalid_argument("Duplicate option: " + option);
        }
        if (index + 1 >= arguments.size() || arguments[index + 1].empty()
            || arguments[index + 1].starts_with("--")) {
            throw std::invalid_argument("Missing value for " + option);
        }
        const std::string& value = arguments[++index];
        if (option == "--frames") {
            frames = parse_frames(value);
            saw_frames = true;
        } else {
            output = value;
            saw_output = true;
        }
    }
    if (!saw_frames) {
        throw std::invalid_argument("trace requires --frames N.");
    }
    std::filesystem::path destination = saw_output
        ? utf8_path(output)
        : std::filesystem::current_path() / (gpu ? "lykoware-gpu-trace.json" : "lykoware-cpu-trace.json");
    destination = std::filesystem::absolute(destination);
    const std::filesystem::path sandbox = find_sandbox(program);
    if (sandbox.empty()) {
        std::cerr << "lyko: cannot launch lykoware_sandbox (not found).\n";
        return lykoware::cli::exit_not_launched;
    }
    if (gpu) {
        // The sandbox's Vulkan window, without sound (ADR 0039).
        return lykoware::cli::run_process(path_to_utf8(sandbox),
                                          {"--window", "--vulkan", "--no-audio", "--frames", std::to_string(frames),
                                           "--trace", path_to_utf8(destination)},
                                          std::filesystem::current_path());
    }
    return lykoware::cli::run_process(
        path_to_utf8(sandbox),
        {"--frames", std::to_string(frames), "--trace", path_to_utf8(destination)},
        std::filesystem::current_path());
}

int route(std::string_view program, const std::vector<std::string>& arguments)
{
    if (arguments.empty() || (arguments.size() == 1 && arguments[0] == "--help")) {
        usage();
        return lykoware::cli::exit_ok;
    }
    if (arguments.size() == 1 && arguments[0] == "--version") {
        std::cout << "lyko " << lykoware::version << '\n';
        return lykoware::cli::exit_ok;
    }

    struct Command {
        std::string_view name;
        Handler handler;
    };
    static constexpr Command commands[] = {
        {"build", build_command},
        {"test", test_command},
        {"run", run_command},
        {"screenshot", screenshot_command},
        {"trace", trace_command},
        {"animate-character", [](std::string_view, std::span<const std::string> args) { return lykoware::cli::animation_command(args); }},
        {"inspect-character", [](std::string_view, std::span<const std::string> args) { return lykoware::cli::character_command(args); }},
    };
    const std::string& name = arguments[0];
    const std::span<const std::string> rest{arguments.data() + 1, arguments.size() - 1};
    for (const Command& command : commands) {
        if (command.name == name) {
            return command.handler(program, rest);
        }
    }
    throw std::invalid_argument("Unknown command: " + name + ". Use --help.");
}
}

namespace lykoware::cli {
int dispatch(std::string_view program, const std::vector<std::string>& arguments)
{
    try {
        return route(program, arguments);
    } catch (const std::exception& error) {
        std::cerr << "lyko: " << error.what() << '\n';
        return exit_usage;
    }
}
}
