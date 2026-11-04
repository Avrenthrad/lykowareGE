#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace lykoware {

// Real time between frames, from std::chrono::steady_clock (monotonic; not
// wall-clock time, not CPU time, not a GPU timer).
//
// Ownership: no heap memory. Lifetime: timing starts at construction.
// Threading: one clock is used from one thread; there is no lock.
// Failure: nothing throws. A clock that steps backwards reports zero.
class FrameClock {
public:
    FrameClock() noexcept;

    // Nanoseconds since construction or the previous lap(), never negative.
    [[nodiscard]] std::int64_t lap() noexcept;

private:
    std::chrono::steady_clock::time_point last_;
};

// Limits for FixedStep. The defaults suit a game on the proposed desktop
// (60 FPS) and handheld (30 FPS) targets: 60 simulation ticks a second
// whatever the frame rate (ADR 0017).
struct FixedStepConfig {
    // Simulation ticks per second: 1 to max_ticks_per_second.
    std::uint32_t ticks_per_second = 60;
    // Real time one frame may feed in, from 1 ns to max_frame_limit_ns.
    // Anything longer (a breakpoint, a long load, a dragged window) is dropped
    // instead of being simulated in one burst.
    std::int64_t max_frame_ns = 250'000'000;
    // Ticks one frame may run, at least 1. Time owed beyond that is dropped,
    // so a machine too slow to keep up runs the simulation slower instead of
    // falling further behind each frame.
    std::uint32_t max_ticks_per_frame = 8;
};

inline constexpr std::uint32_t max_ticks_per_second = 1000;
inline constexpr std::int64_t max_frame_limit_ns = 10'000'000'000;

// What one frame should do, from FixedStep::advance().
struct FrameSteps {
    // Run simulation ticks first_tick, first_tick + 1, ... first_tick + ticks - 1.
    std::uint64_t first_tick = 0;
    std::uint32_t ticks = 0;
    // How far real time has passed beyond the last tick, as a fraction of a
    // tick in [0, 1). For drawing only: blend the last two simulation states
    // by alpha. Simulation code must never read it.
    double alpha = 0.0;
    // Real time discarded this frame by max_frame_ns or max_ticks_per_frame,
    // in nanoseconds (rounded down). Non-zero means the simulation fell behind.
    std::int64_t dropped_ns = 0;
};

// The simulation clock: turns variable real frame times into a whole number
// of fixed-length ticks (an accumulator with interpolation alpha).
//
// Determinism: the tick count after any sequence of frames depends only on
// the total real time fed in (less anything dropped), not on how that time
// was split into frames. Time is kept as an exact integer fraction, so any
// integer tick rate runs exactly that many ticks per second of input with
// no rounding drift, however long the program runs. Simulation code that
// advances only per tick, by tick_seconds(), gives the same result at any
// frame rate.
//
// Ownership: a plain value with no heap memory; copyable.
// Threading: one thread at a time; there is no lock.
// Failure: create() returns nullopt and fills error for invalid limits.
// advance() does not throw; negative elapsed time counts as zero.
class FixedStep {
public:
    [[nodiscard]] static std::optional<FixedStep> create(const FixedStepConfig& config, std::string& error);

    // Feeds one frame's real elapsed time and returns the ticks to run.
    [[nodiscard]] FrameSteps advance(std::int64_t elapsed_ns) noexcept;

    // Ticks issued so far: the index of the next tick advance() will issue.
    [[nodiscard]] std::uint64_t tick() const noexcept;
    [[nodiscard]] std::uint32_t ticks_per_second() const noexcept;
    // Simulation time step, 1 / ticks_per_second(), in seconds.
    [[nodiscard]] double tick_seconds() const noexcept;
    // Simulation time at the start of `tick`, in nanoseconds (rounded down).
    [[nodiscard]] std::int64_t tick_start_ns(std::uint64_t tick) const noexcept;
    // Total real time dropped since creation, in nanoseconds (rounded down).
    [[nodiscard]] std::int64_t total_dropped_ns() const noexcept;
    [[nodiscard]] const FixedStepConfig& config() const noexcept;

private:
    explicit FixedStep(const FixedStepConfig& config) noexcept;

    FixedStepConfig config_;
    // Real time not yet turned into ticks, in nanoseconds times
    // ticks_per_second, always in [0, one second's nanoseconds).
    std::int64_t remainder_ = 0;
    std::uint64_t tick_ = 0;
    std::int64_t dropped_ns_ = 0;
};

}
