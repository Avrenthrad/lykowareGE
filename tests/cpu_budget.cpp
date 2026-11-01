#include <lykoware/trace.hpp>

#include "../tools/cli/process.hpp"

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace {

// Printed on every budget result so the log states the scope of the numbers.
constexpr std::string_view kScope =
    "provisional CPU-only; pinned hosted runners windows-2022 and ubuntu-24.04";

bool exceeded(std::int64_t elapsed_us, std::int64_t limit_ms)
{
    if (limit_ms < 0) {
        return true;
    }
    constexpr auto max_ms = std::numeric_limits<std::int64_t>::max() / 1000;
    if (limit_ms > max_ms) {
        return false;
    }
    return elapsed_us > limit_ms * 1000;
}

bool parse_i64(std::string_view text, std::int64_t& value)
{
    if (text.empty() || text.front() == '+') {
        return false;
    }
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

void report(std::string_view label, std::int64_t elapsed_us, std::int64_t limit_ms, bool over)
{
    const char* verdict = over ? "exceeded" : "within";
    std::cout << kScope << "; " << verdict << "; " << label
              << "; elapsed_us=" << elapsed_us
              << "; limit_ms=" << limit_ms << '\n';
}

int check_budget(std::string_view label, std::int64_t elapsed_us, std::int64_t limit_ms)
{
    const bool over = exceeded(elapsed_us, limit_ms);
    report(label, elapsed_us, limit_ms, over);
    return over ? 1 : 0;
}

int fixture(std::uint64_t iterations, std::int64_t limit_ms)
{
    lykoware::CpuTimer timer;
    std::uint64_t state = 0x6c796b6f77617265ULL;
    for (std::uint64_t index = 0; index < iterations; ++index) {
        state = lykoware::cpu_fixture_iteration(state ^ (index + 1));
    }
    const std::int64_t elapsed = timer.elapsed_us();
    if (state == 0) {
        std::cout << "cpu fixture accumulator is zero.\n";
    }
    return check_budget("cpu-fixture", elapsed, limit_ms);
}

int startup(const std::string& sandbox, std::int64_t limit_ms)
{
    lykoware::CpuTimer timer;
    const int status = lykoware::cli::run_process(
        sandbox, {}, std::filesystem::current_path());
    const std::int64_t elapsed = timer.elapsed_us();
    if (status != 0) {
        report("sandbox-startup", elapsed, limit_ms, true);
        std::cerr << "sandbox startup exited " << status << ".\n";
        return status == 1 ? 2 : status;
    }
    return check_budget("sandbox-startup", elapsed, limit_ms);
}

}

int main(int argc, char* argv[])
{
    try {
        if (argc == 5 && std::string_view{argv[1]} == "check") {
            std::int64_t elapsed_us = 0;
            std::int64_t limit_ms = 0;
            const std::string_view expect{argv[4]};
            if (!parse_i64(argv[2], elapsed_us) || !parse_i64(argv[3], limit_ms)
                || (expect != "within" && expect != "over")) {
                std::cerr << "usage: check ELAPSED_US LIMIT_MS within|over\n";
                return 2;
            }
            const int status = check_budget("comparison", elapsed_us, limit_ms);
            const bool over = status != 0;
            if ((expect == "over") != over) {
                std::cerr << "budget comparison did not match " << expect << ".\n";
                return 1;
            }
            return 0;
        }
        if (argc == 4 && std::string_view{argv[1]} == "fixture") {
            std::int64_t iterations = 0;
            std::int64_t limit_ms = 0;
            if (!parse_i64(argv[2], iterations) || iterations < 0
                || !parse_i64(argv[3], limit_ms)) {
                std::cerr << "usage: fixture ITERATIONS LIMIT_MS\n";
                return 2;
            }
            return fixture(static_cast<std::uint64_t>(iterations), limit_ms);
        }
        if (argc == 4 && std::string_view{argv[1]} == "startup") {
            std::int64_t limit_ms = 0;
            if (!parse_i64(argv[3], limit_ms)) {
                std::cerr << "usage: startup SANDBOX LIMIT_MS\n";
                return 2;
            }
            return startup(argv[2], limit_ms);
        }
    } catch (const std::exception& error) {
        std::cerr << "cpu budget check failed: " << error.what() << '\n';
        return 2;
    }
    std::cerr << "usage: startup SANDBOX LIMIT_MS | fixture ITERATIONS LIMIT_MS"
                 " | check ELAPSED_US LIMIT_MS within|over\n";
    return 2;
}
