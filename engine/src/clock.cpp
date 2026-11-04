#include <lykoware/clock.hpp>

#include <algorithm>

namespace lykoware {
namespace {

// One tick in the accumulator's units (nanoseconds times ticks per second):
// a tick lasts 1e9 / rate ns, so in those units it is exactly 1e9.
constexpr std::int64_t one_tick = 1'000'000'000;

}

FrameClock::FrameClock() noexcept
    : last_{std::chrono::steady_clock::now()}
{
}

std::int64_t FrameClock::lap() noexcept
{
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(now - last_).count();
    last_ = now;
    return elapsed < 0 ? 0 : static_cast<std::int64_t>(elapsed);
}

std::optional<FixedStep> FixedStep::create(const FixedStepConfig& config, std::string& error)
{
    error.clear();
    if (config.ticks_per_second < 1 || config.ticks_per_second > max_ticks_per_second) {
        error = "ticks_per_second must be from 1 to " + std::to_string(max_ticks_per_second);
        return std::nullopt;
    }
    if (config.max_frame_ns < 1 || config.max_frame_ns > max_frame_limit_ns) {
        error = "max_frame_ns must be from 1 to " + std::to_string(max_frame_limit_ns);
        return std::nullopt;
    }
    if (config.max_ticks_per_frame < 1) {
        error = "max_ticks_per_frame must be at least 1";
        return std::nullopt;
    }
    return FixedStep{config};
}

FixedStep::FixedStep(const FixedStepConfig& config) noexcept
    : config_{config}
{
}

FrameSteps FixedStep::advance(std::int64_t elapsed_ns) noexcept
{
    const std::int64_t rate = config_.ticks_per_second;
    FrameSteps steps;
    steps.first_tick = tick_;

    std::int64_t elapsed = std::max<std::int64_t>(elapsed_ns, 0);
    if (elapsed > config_.max_frame_ns) {
        steps.dropped_ns = elapsed - config_.max_frame_ns;
        elapsed = config_.max_frame_ns;
    }
    // At most 1e10 ns * 1000 + 1e9: far inside int64.
    remainder_ += elapsed * rate;
    const std::int64_t due = remainder_ / one_tick;
    const std::int64_t run = std::min<std::int64_t>(due, config_.max_ticks_per_frame);
    // Whole ticks leave the accumulator whether they run or are dropped; the
    // fraction of a tick stays for the next frame.
    remainder_ -= due * one_tick;
    if (due > run) {
        steps.dropped_ns += (due - run) * one_tick / rate;
    }

    steps.ticks = static_cast<std::uint32_t>(run);
    tick_ += static_cast<std::uint64_t>(run);
    steps.alpha = static_cast<double>(remainder_) / static_cast<double>(one_tick);
    dropped_ns_ += steps.dropped_ns;
    return steps;
}

std::uint64_t FixedStep::tick() const noexcept
{
    return tick_;
}

std::uint32_t FixedStep::ticks_per_second() const noexcept
{
    return config_.ticks_per_second;
}

double FixedStep::tick_seconds() const noexcept
{
    return 1.0 / static_cast<double>(config_.ticks_per_second);
}

std::int64_t FixedStep::tick_start_ns(std::uint64_t tick) const noexcept
{
    // Split so tick * 1e9 cannot overflow for any realistic run length.
    const std::uint64_t rate = config_.ticks_per_second;
    const std::uint64_t whole_seconds = tick / rate;
    const std::uint64_t part = tick % rate;
    return static_cast<std::int64_t>(whole_seconds * 1'000'000'000ULL + part * 1'000'000'000ULL / rate);
}

std::int64_t FixedStep::total_dropped_ns() const noexcept
{
    return dropped_ns_;
}

const FixedStepConfig& FixedStep::config() const noexcept
{
    return config_;
}

}
