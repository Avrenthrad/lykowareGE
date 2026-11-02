#include <lykoware/log.hpp>
#include <lykoware/trace.hpp>
#include <lykoware/version.hpp>
#include <lykoware/window.hpp>

#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
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

std::uint64_t parse_frames(std::string_view text)
{
    if (text.empty() || text.front() == '+' || text.front() == '-') {
        throw std::invalid_argument(
            "--frames must be an integer from 1 to "
            + std::to_string(lykoware::cpu_trace_frame_limit) + ".");
    }
    std::uint64_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()
        || value < 1 || value > lykoware::cpu_trace_frame_limit) {
        throw std::invalid_argument(
            "--frames must be an integer from 1 to "
            + std::to_string(lykoware::cpu_trace_frame_limit) + ".");
    }
    return value;
}

int write_cpu_trace(std::uint64_t frames, const std::filesystem::path& path)
{
    lykoware::CpuTrace trace;
    std::uint64_t state = 0x6c796b6f77617265ULL;
    {
        lykoware::CpuTrace::Zone loop{trace, "sandbox.loop"};
        for (std::uint64_t index = 0; index < frames; ++index) {
            lykoware::CpuTrace::Zone frame{trace, "sandbox.frame"};
            state = lykoware::cpu_fixture_iteration(state ^ (index + 1));
        }
    }
    if (state == 0) {
        lykoware::log(lykoware::LogLevel::info, "CPU fixture accumulator is zero.");
    }
    if (!trace.write(path)) {
        lykoware::log(lykoware::LogLevel::error, "Could not write CPU trace.");
        return EXIT_FAILURE;
    }
    lykoware::log(lykoware::LogLevel::info, "Wrote CPU trace.");
    return EXIT_SUCCESS;
}

int run_trace(const std::vector<std::string>& args)
{
    bool saw_frames = false;
    bool saw_trace = false;
    std::uint64_t frames = 0;
    std::string trace_file;
    for (std::size_t index = 0; index < args.size(); ++index) {
        const std::string& option = args[index];
        if (option != "--frames" && option != "--trace") {
            throw std::invalid_argument("Unknown option: " + option + ". Use --help.");
        }
        if ((option == "--frames" && saw_frames) || (option == "--trace" && saw_trace)) {
            throw std::invalid_argument("Duplicate option: " + option);
        }
        if (index + 1 >= args.size() || args[index + 1].empty()
            || args[index + 1].starts_with("--")) {
            throw std::invalid_argument("Missing value for " + option);
        }
        const std::string& value = args[++index];
        if (option == "--frames") {
            frames = parse_frames(value);
            saw_frames = true;
        } else {
            trace_file = value;
            saw_trace = true;
        }
    }
    if (!saw_frames || !saw_trace) {
        throw std::invalid_argument("CPU trace requires --frames N and --trace FILE.");
    }
    return write_cpu_trace(frames, utf8_path(trace_file));
}

bool has_option(const std::vector<std::string>& args, std::string_view name)
{
    for (const std::string& arg : args) {
        if (arg == name) {
            return true;
        }
    }
    return false;
}

int run_window(const std::vector<std::string>& args)
{
    bool saw_window = false;
    bool saw_dummy = false;
    bool saw_frames = false;
    std::uint64_t frames = 0;
    for (std::size_t index = 0; index < args.size(); ++index) {
        const std::string& option = args[index];
        if (option == "--window") {
            if (saw_window) {
                throw std::invalid_argument("Duplicate option: --window");
            }
            saw_window = true;
            continue;
        }
        if (option == "--dummy") {
            if (saw_dummy) {
                throw std::invalid_argument("Duplicate option: --dummy");
            }
            saw_dummy = true;
            continue;
        }
        if (option != "--frames") {
            throw std::invalid_argument("Unknown option: " + option + ". Use --help.");
        }
        if (saw_frames) {
            throw std::invalid_argument("Duplicate option: --frames");
        }
        if (index + 1 >= args.size() || args[index + 1].empty()
            || args[index + 1].starts_with("--")) {
            throw std::invalid_argument("Missing value for --frames");
        }
        frames = parse_frames(args[++index]);
        saw_frames = true;
    }
    if (!saw_window) {
        throw std::invalid_argument("Expected --window.");
    }
    if (saw_dummy && !saw_frames) {
        throw std::invalid_argument(
            "--window --dummy requires --frames N so it can exit without a display.");
    }

    lykoware::WindowConfig config;
    config.title = "lykoware sandbox";
    config.width = 960;
    config.height = 540;
    config.resizable = true;
    config.visible = !saw_dummy;
    config.vulkan = false;
    config.driver = saw_dummy ? lykoware::VideoDriver::Dummy : lykoware::VideoDriver::Platform;
    std::string error;
    const std::unique_ptr<lykoware::Window> window = lykoware::Window::open(config, error);
    if (!window) {
        lykoware::log(lykoware::LogLevel::error, error);
        return EXIT_FAILURE;
    }
    lykoware::log(lykoware::LogLevel::info, "Opened sandbox window.");
    if (saw_frames) {
        for (std::uint64_t index = 0; index < frames; ++index) {
            if (!window->pump()) {
                break;
            }
        }
    } else {
        while (window->pump()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }
    lykoware::log(lykoware::LogLevel::info, "Closed sandbox window.");
    return EXIT_SUCCESS;
}

int run(const std::vector<std::string>& args)
{
    if (has_option(args, "--window")) {
        return run_window(args);
    }
    if (has_option(args, "--frames") || has_option(args, "--trace")) {
        return run_trace(args);
    }
    if (args.size() > 1) {
        throw std::invalid_argument("Expected at most one option. Use --help.");
    }
    if (args.size() == 1) {
        const std::string_view option{args[0]};
        if (option == "--version") {
            std::cout << "lykowareGE " << lykoware::version << '\n';
            return EXIT_SUCCESS;
        }
        if (option == "--help") {
            std::cout << "Usage: lykoware_sandbox [--version|--help]\n"
                         "       lykoware_sandbox --frames N --trace FILE\n"
                         "       lykoware_sandbox --window [--dummy] [--frames N]\n"
                         "Without options, runs the console foundation check.\n"
                         "--frames with --trace runs N CPU iterations and writes a Chrome trace.\n"
                         "--window opens a window and pumps input until it closes.\n"
                         "--dummy selects the headless video driver and requires --frames.\n";
            return EXIT_SUCCESS;
        }
        throw std::invalid_argument("Unknown option: " + std::string{option}
                                    + ". Use --help.");
    }

    lykoware::log(lykoware::LogLevel::info,
                  "Starting lykowareGE " + std::string{lykoware::version});
    lykoware::log(lykoware::LogLevel::info,
                  "Engine library linked; sandbox bootstrap ready.");
    lykoware::log(lykoware::LogLevel::info,
                  "Foundation check complete. Pass --window to open one.");
    return EXIT_SUCCESS;
}

int fail(const std::exception& error)
{
    lykoware::log(lykoware::LogLevel::error, error.what());
    return EXIT_FAILURE;
}

}

#ifdef _WIN32
namespace {
std::string utf8_from_wide(const wchar_t* text)
{
    if (text == nullptr) {
        throw std::runtime_error("Invalid Unicode command argument.");
    }
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1,
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        throw std::runtime_error("Invalid Unicode command argument.");
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, result.data(),
                            size, nullptr, nullptr) == 0) {
        throw std::runtime_error("Unable to convert command argument.");
    }
    result.pop_back();
    return result;
}
}

int wmain(int argc, wchar_t* argv[])
{
    try {
        std::vector<std::string> arguments;
        for (int index = 1; index < argc; ++index) {
            arguments.push_back(utf8_from_wide(argv[index]));
        }
        return run(arguments);
    } catch (const std::exception& error) {
        return fail(error);
    } catch (...) {
        lykoware::log(lykoware::LogLevel::error, "Unknown startup failure.");
    }
    return EXIT_FAILURE;
}
#else
int main(int argc, char* argv[])
{
    try {
        std::vector<std::string> arguments;
        if (argc > 1) {
            arguments.assign(argv + 1, argv + argc);
        }
        return run(arguments);
    } catch (const std::exception& error) {
        return fail(error);
    } catch (...) {
        lykoware::log(lykoware::LogLevel::error, "Unknown startup failure.");
    }
    return EXIT_FAILURE;
}
#endif
