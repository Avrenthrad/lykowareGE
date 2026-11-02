#include <lykoware/trace.hpp>

#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>

namespace {

int g_failures = 0;

void expect(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

std::string read_file(const std::filesystem::path& path)
{
    std::ifstream input{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

std::int64_t number_after(const std::string& json, std::string_view key, std::size_t from)
{
    const auto at = json.find(key, from);
    expect(at != std::string::npos, "missing numeric field");
    if (at == std::string::npos) {
        return -1;
    }
    const char* begin = json.data() + at + key.size();
    const char* end = json.data() + json.size();
    std::int64_t value = 0;
    const auto parsed = std::from_chars(begin, end, value);
    expect(parsed.ec == std::errc{}, "numeric field is not an integer");
    return value;
}

struct EventTimes {
    std::size_t position = std::string::npos;
    std::int64_t timestamp_us = -1;
    std::int64_t duration_us = -1;
};

EventTimes event_times(const std::string& json, std::string_view name)
{
    const std::string marker = std::string{"\"name\":\""} + std::string{name} + "\"";
    EventTimes times;
    times.position = json.find(marker);
    expect(times.position != std::string::npos, "missing zone");
    if (times.position == std::string::npos) {
        return times;
    }
    times.timestamp_us = number_after(json, "\"ts\":", times.position);
    times.duration_us = number_after(json, "\"dur\":", times.position);
    return times;
}

void test_timer_and_fixture()
{
    lykoware::CpuTimer timer;
    expect(timer.elapsed_us() >= 0, "timer starts at a non-negative elapsed time");
    const std::uint64_t first = lykoware::cpu_fixture_iteration(0x1234);
    const std::uint64_t second = lykoware::cpu_fixture_iteration(0x1234);
    expect(first == second, "fixture iteration is deterministic");
    expect(first == 0xce8c283f9e1031a3ULL, "fixture iteration matches the pinned mix");
    expect(lykoware::cpu_fixture_iteration(0) == 0x7ab928e0e5a07ed4ULL, "fixture seed 0");
    expect(lykoware::cpu_fixture_iteration(1) == 0x81e1823e9536849dULL, "fixture seed 1");
    expect(first != 0x1234ULL, "fixture iteration changes its input");

    std::uint64_t state = 1;
    for (int step = 0; step < 100000; ++step) {
        state = lykoware::cpu_fixture_iteration(state);
    }
    expect(timer.elapsed_us() > 0, "timer advances across fixture work");
    expect(state != 1, "repeated fixture work changes the accumulator");
    timer.reset();
    expect(timer.elapsed_us() >= 0, "reset timer stays non-negative");
}

void test_empty_trace(const std::filesystem::path& path)
{
    lykoware::CpuTrace trace;
    expect(trace.completed_count() == 0, "new trace has no events");
    expect(trace.write(path), "empty trace writes");
    expect(read_file(path) == "{\"displayTimeUnit\":\"ms\",\"traceEvents\":[]}\n",
           "empty Chrome trace document");
}

void test_scoped_zones(const std::filesystem::path& path)
{
    lykoware::CpuTrace trace;
    {
        lykoware::CpuTrace::Zone outer{trace, "outer"};
        expect(trace.completed_count() == 0, "open zone is not complete");
        expect(!trace.write(path), "write rejects an open zone");
        expect(!std::filesystem::exists(path), "rejected write creates no file");
        {
            lykoware::CpuTrace::Zone inner{trace, "inner"};
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        expect(trace.completed_count() == 1, "inner zone completed");
    }
    expect(trace.completed_count() == 2, "outer zone completed");
    expect(trace.write(path), "closed trace writes");

    const std::string json = read_file(path);
    expect(json.starts_with("{\"displayTimeUnit\":\"ms\",\"traceEvents\":["), "trace prefix");
    expect(json.ends_with("]}\n"), "trace suffix");
    expect(json.find("\"ph\":\"X\"") != std::string::npos, "complete event phase");
    expect(json.find("\"cat\":\"cpu\"") != std::string::npos, "cpu category");
    expect(json.find("\"pid\":1,\"tid\":1") != std::string::npos, "single-thread ids");
    const EventTimes inner = event_times(json, "inner");
    const EventTimes outer = event_times(json, "outer");
    expect(inner.position < outer.position, "events are stored in completion order");
    expect(inner.timestamp_us >= outer.timestamp_us, "inner zone starts after the outer zone");
    expect(inner.duration_us >= 0 && outer.duration_us >= 0, "durations are non-negative");
    expect(outer.duration_us >= inner.duration_us, "outer zone covers the inner zone");
}

void test_escaping_and_replacement(const std::filesystem::path& path)
{
    const std::string_view name = "\"quote\\and\n\x01";
    {
        lykoware::CpuTrace trace;
        {
            lykoware::CpuTrace::Zone zone{trace, name};
        }
        expect(trace.write(path), "escaped name writes");
    }
    const std::string json = read_file(path);
    expect(json.find(R"("name":"\"quote\\and\n\u0001")") != std::string::npos,
           "name is escaped by hand");
    expect(json.find("\"ph\":\"X\"") != std::string::npos, "escaped event keeps its phase");

    lykoware::CpuTrace replacement;
    expect(replacement.write(path), "write replaces an existing trace");
    expect(read_file(path) == "{\"displayTimeUnit\":\"ms\",\"traceEvents\":[]}\n",
           "replacement removes the previous events");
}

void test_write_failure(const std::filesystem::path& root)
{
    lykoware::CpuTrace trace;
    {
        lykoware::CpuTrace::Zone zone{trace, "kept"};
    }
    expect(!trace.write(root), "a directory path is not a trace file");
    expect(trace.completed_count() == 1, "failed write keeps recorded events");
    const auto missing = root / "missing-parent" / "trace.json";
    expect(!trace.write(missing), "missing parent directory fails");
    expect(!std::filesystem::exists(missing), "failed write leaves no trace file");
    expect(trace.write(root / "recovered.json"), "events remain writable after a failure");
}

}

int main()
{
    const auto root = std::filesystem::temp_directory_path() / "lykoware-trace-api";
    std::filesystem::remove_all(root);
    std::filesystem::create_directory(root);

    test_timer_and_fixture();
    test_empty_trace(root / "empty.json");
    test_scoped_zones(root / "zones.json");
    test_escaping_and_replacement(root / "escaped.json");
    test_write_failure(root);

    std::filesystem::remove_all(root);
    if (g_failures != 0) {
        std::cerr << g_failures << " trace API check(s) failed.\n";
        return 1;
    }
    std::cout << "trace API checks passed.\n";
    return 0;
}
