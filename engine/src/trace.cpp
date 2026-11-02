#include <lykoware/trace.hpp>

#include <charconv>
#include <fstream>
#include <system_error>
#include <utility>
#include <vector>

namespace lykoware {
namespace {

struct Event {
    std::string name;
    std::int64_t timestamp_us = 0;
    std::int64_t duration_us = 0;
};

void append_number(std::string& out, std::int64_t value)
{
    char buffer[32];
    const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (converted.ec != std::errc{}) {
        out += '0';
        return;
    }
    out.append(buffer, static_cast<std::size_t>(converted.ptr - buffer));
}

void append_escaped(std::string& out, std::string_view text)
{
    constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char character : text) {
        switch (character) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (character < 0x20) {
                out += "\\u00";
                out += hex[character >> 4];
                out += hex[character & 0x0f];
            } else {
                out += static_cast<char>(character);
            }
            break;
        }
    }
}

std::string format_trace(const std::vector<Event>& events)
{
    std::string json;
    json.reserve(64 + events.size() * 48);
    json += "{\"displayTimeUnit\":\"ms\",\"traceEvents\":[";
    bool first = true;
    for (const Event& event : events) {
        if (!first) {
            json += ',';
        }
        first = false;
        json += "{\"name\":\"";
        append_escaped(json, event.name);
        json += "\",\"cat\":\"cpu\",\"ph\":\"X\",\"ts\":";
        append_number(json, event.timestamp_us);
        json += ",\"dur\":";
        append_number(json, event.duration_us);
        json += ",\"pid\":1,\"tid\":1}";
    }
    json += "]}\n";
    return json;
}

}

struct CpuTrace::State {
    std::chrono::steady_clock::time_point origin{std::chrono::steady_clock::now()};
    std::vector<Event> events;
    int open_zones = 0;
    bool failed = false;
};

CpuTimer::CpuTimer() noexcept
    : start_{std::chrono::steady_clock::now()}
{
}

void CpuTimer::reset() noexcept
{
    start_ = std::chrono::steady_clock::now();
}

std::int64_t CpuTimer::elapsed_us() const noexcept
{
    const auto elapsed = std::chrono::steady_clock::now() - start_;
    const auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
    return microseconds < 0 ? std::int64_t{0} : microseconds;
}

std::uint64_t cpu_fixture_iteration(std::uint64_t state) noexcept
{
    for (int round = 0; round < 32; ++round) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        state *= 0x9E3779B185EBCA87ULL;
        state ^= state >> 30;
        state += static_cast<std::uint64_t>(round);
    }
    return state;
}

CpuTrace::CpuTrace()
    : state_{std::make_unique<State>()}
{
}

CpuTrace::~CpuTrace() = default;

CpuTrace::Zone::Zone(CpuTrace& trace, std::string_view name)
    : trace_{trace}
    , name_{name}
    , start_us_{trace.now_us()}
    , open_{false}
{
    trace_.begin_zone();
    open_ = true;
}

CpuTrace::Zone::~Zone()
{
    if (!open_) {
        return;
    }
    open_ = false;
    trace_.end_zone(std::move(name_), start_us_);
}

bool CpuTrace::write(const std::filesystem::path& path) const
{
    if (!state_ || state_->failed || state_->open_zones != 0) {
        return false;
    }
    const std::string json = format_trace(state_->events);
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    if (!output) {
        return false;
    }
    output.write(json.data(), static_cast<std::streamsize>(json.size()));
    output.close();
    if (!output) {
        std::error_code error;
        std::filesystem::remove(path, error);
        return false;
    }
    return true;
}

std::size_t CpuTrace::completed_count() const noexcept
{
    return state_ ? state_->events.size() : 0;
}

void CpuTrace::begin_zone()
{
    ++state_->open_zones;
}

void CpuTrace::end_zone(std::string name, std::int64_t start_us) noexcept
{
    if (state_->open_zones > 0) {
        --state_->open_zones;
    }
    try {
        std::int64_t duration = now_us() - start_us;
        if (duration < 0) {
            duration = 0;
        }
        state_->events.push_back(Event{std::move(name), start_us, duration});
    } catch (...) {
        state_->failed = true;
    }
}

std::int64_t CpuTrace::now_us() const noexcept
{
    const auto elapsed = std::chrono::steady_clock::now() - state_->origin;
    const auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
    return microseconds < 0 ? std::int64_t{0} : microseconds;
}

}
