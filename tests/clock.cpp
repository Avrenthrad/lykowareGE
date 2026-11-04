// clock.fixed_step: the simulation clock (ADR 0017). Every case feeds exact
// frame times, so results are deterministic; only the FrameClock case reads
// the real clock, and it checks a lower bound.

#include <lykoware/clock.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

int g_failures = 0;

void expect(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

constexpr std::int64_t ms = 1'000'000;
constexpr std::int64_t second = 1'000'000'000;

lykoware::FixedStep make(std::uint32_t rate, std::uint32_t max_ticks = 8, std::int64_t max_frame = 250 * ms)
{
    lykoware::FixedStepConfig config;
    config.ticks_per_second = rate;
    config.max_ticks_per_frame = max_ticks;
    config.max_frame_ns = max_frame;
    std::string error;
    std::optional<lykoware::FixedStep> clock = lykoware::FixedStep::create(config, error);
    if (!clock) {
        std::cerr << "FATAL: valid config refused: " << error << '\n';
        std::exit(EXIT_FAILURE);
    }
    return *clock;
}

// Feeds frames and checks that tick ranges are contiguous. Returns ticks run.
std::uint64_t feed(lykoware::FixedStep& clock, const std::vector<std::int64_t>& frames)
{
    std::uint64_t ran = 0;
    for (const std::int64_t frame : frames) {
        const std::uint64_t before = clock.tick();
        const lykoware::FrameSteps steps = clock.advance(frame);
        if (steps.first_tick != before || clock.tick() != before + steps.ticks) {
            expect(false, "tick ranges are contiguous");
            break;
        }
        ran += steps.ticks;
    }
    return ran;
}

// `count` frames of `each` ns, then one frame topping the total up to `total`.
std::vector<std::int64_t> paced(std::int64_t total, std::int64_t each)
{
    std::vector<std::int64_t> frames;
    std::int64_t sum = 0;
    while (sum + each <= total) {
        frames.push_back(each);
        sum += each;
    }
    if (sum < total) {
        frames.push_back(total - sum);
    }
    return frames;
}

// Irregular frames from 1 to 40 ms (a fixed-seed LCG), summing to `total`.
std::vector<std::int64_t> jittered(std::int64_t total, std::uint64_t seed)
{
    std::vector<std::int64_t> frames;
    std::int64_t sum = 0;
    std::uint64_t state = seed;
    while (sum < total) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        std::int64_t frame = 1 * ms + static_cast<std::int64_t>((state >> 33) % (39 * ms));
        if (sum + frame > total) {
            frame = total - sum;
        }
        frames.push_back(frame);
        sum += frame;
    }
    return frames;
}

void test_config_validation()
{
    std::string error;
    lykoware::FixedStepConfig config;
    expect(lykoware::FixedStep::create(config, error).has_value() && error.empty(), "the defaults are valid");
    expect(config.ticks_per_second == 60 && config.max_ticks_per_frame == 8 && config.max_frame_ns == 250 * ms,
           "defaults are 60 Hz, 8 ticks and 250 ms per frame");

    const auto refused = [&error](lykoware::FixedStepConfig bad, std::string_view what) {
        const bool rejected = !lykoware::FixedStep::create(bad, error).has_value() && !error.empty();
        expect(rejected, what);
    };
    lykoware::FixedStepConfig bad = config;
    bad.ticks_per_second = 0;
    refused(bad, "0 ticks per second is refused");
    bad.ticks_per_second = lykoware::max_ticks_per_second + 1;
    refused(bad, "too many ticks per second is refused");
    bad = config;
    bad.max_frame_ns = 0;
    refused(bad, "a zero frame limit is refused");
    bad.max_frame_ns = lykoware::max_frame_limit_ns + 1;
    refused(bad, "an over-long frame limit is refused");
    bad = config;
    bad.max_ticks_per_frame = 0;
    refused(bad, "zero ticks per frame is refused");
}

void test_exact_rate()
{
    lykoware::FixedStep clock = make(60);
    expect(feed(clock, paced(second, 1 * ms)) == 60, "one second in 1 ms frames is exactly 60 ticks");
    expect(clock.advance(0).alpha == 0.0, "nothing is left over after exactly one second");

    // One hour of frames rounded to whole nanoseconds (16,666,667 ns, slightly
    // long): still exactly 216,000 ticks, with no drift.
    lykoware::FixedStep hour = make(60);
    std::uint64_t ran = 0;
    for (int frame = 0; frame < 216'000; ++frame) {
        ran += hour.advance(16'666'667).ticks;
    }
    expect(ran == 216'000 && hour.tick() == 216'000, "an hour at 60 FPS is exactly 216000 ticks");
    expect(hour.total_dropped_ns() == 0, "nothing is dropped at a steady 60 FPS");

    // 1000 hours in 10 s frames. Rounding 1/60 s to whole nanoseconds loses
    // under 1 ns per tick, which only shows up over a long run: an
    // accumulator in whole nanoseconds would gain 8 extra ticks here.
    lykoware::FixedStep long_run = make(60, 1000, 10 * second);
    for (int frame = 0; frame < 360'000; ++frame) {
        (void)long_run.advance(10 * second);
    }
    expect(long_run.tick() == 216'000'000 && long_run.total_dropped_ns() == 0,
           "1000 hours is exactly 216,000,000 ticks: no drift");

    lykoware::FixedStep fast = make(1000);
    expect(feed(fast, paced(second, 7 * ms)) == 1000, "1000 Hz: one second is 1000 ticks");
    lykoware::FixedStep slow = make(1);
    expect(feed(slow, paced(3 * second, 16 * ms)) == 3, "1 Hz: three seconds is 3 ticks");
}

void test_frame_rate_independence()
{
    const std::int64_t total = 10 * second;
    std::vector<std::vector<std::int64_t>> patterns = {
        paced(total, 33'333'333), // 30 FPS (handheld target)
        paced(total, 16'666'667), // 60 FPS (desktop target)
        paced(total, 6'944'444),  // 144 FPS
        paced(total, 4'166'667),  // 240 FPS
        jittered(total, 1),
        jittered(total, 0x6c796b6f),
    };
    std::vector<double> positions;
    for (const auto& frames : patterns) {
        lykoware::FixedStep clock = make(60);
        // A stand-in simulation that only advances per tick.
        double position = 0.0;
        double velocity = 3.0;
        const std::uint64_t ran = [&clock, &frames, &position, &velocity] {
            std::uint64_t count = 0;
            for (const std::int64_t frame : frames) {
                const lykoware::FrameSteps steps = clock.advance(frame);
                for (std::uint32_t tick = 0; tick < steps.ticks; ++tick) {
                    velocity -= 9.81 * clock.tick_seconds();
                    position += velocity * clock.tick_seconds();
                }
                count += steps.ticks;
            }
            return count;
        }();
        expect(ran == 600, "ten seconds is 600 ticks at any frame rate");
        positions.push_back(position);
    }
    // The same ticks with the same step give bit-identical state.
    for (const double position : positions) {
        expect(position == positions.front(), "the simulated state is identical at any frame rate");
    }
}

void test_alpha()
{
    lykoware::FixedStep clock = make(60);
    lykoware::FrameSteps steps = clock.advance(8'333'333);
    expect(steps.ticks == 0 && std::abs(steps.alpha - 0.5) < 1e-6, "half a tick gives alpha 0.5 and no tick");
    steps = clock.advance(8'333'334);
    expect(steps.ticks == 1 && steps.alpha < 1e-6, "the other half completes one tick");
    steps = clock.advance(25 * ms);
    expect(steps.ticks == 1 && std::abs(steps.alpha - 0.5) < 1e-6, "one and a half ticks: one tick, alpha 0.5");
}

void test_limits()
{
    // A 1 s stall with a 250 ms frame limit: 750 ms dropped, 15 ticks run.
    lykoware::FixedStep stall = make(60, 100);
    lykoware::FrameSteps steps = stall.advance(second);
    expect(steps.ticks == 15 && steps.dropped_ns == 750 * ms, "a long frame is clamped to max_frame_ns");

    // 200 ms owed with at most 4 ticks a frame: 4 run, 8 dropped whole.
    lykoware::FixedStep behind = make(60, 4);
    steps = behind.advance(200 * ms);
    expect(steps.ticks == 4, "no more than max_ticks_per_frame run");
    expect(steps.dropped_ns == 133'333'333, "the ticks not run are reported as dropped time");
    expect(steps.alpha == 0.0, "dropping whole ticks keeps the fraction");
    steps = behind.advance(16'666'667);
    expect(steps.ticks == 1, "the next normal frame runs one tick, with no backlog");
    expect(behind.total_dropped_ns() == 133'333'333, "dropped time is totalled");

    lykoware::FixedStep backwards = make(60);
    steps = backwards.advance(-5 * ms);
    expect(steps.ticks == 0 && steps.alpha == 0.0 && steps.dropped_ns == 0, "negative time counts as zero");
    steps = backwards.advance(0);
    expect(steps.ticks == 0 && backwards.tick() == 0, "zero time runs nothing");
}

void test_tick_times()
{
    const lykoware::FixedStep clock = make(60);
    expect(clock.ticks_per_second() == 60, "the rate is reported");
    expect(clock.tick_seconds() == 1.0 / 60.0, "the step is 1/60 s");
    expect(clock.tick_start_ns(0) == 0 && clock.tick_start_ns(1) == 16'666'666, "tick 1 starts at 16,666,666 ns");
    expect(clock.tick_start_ns(60) == second && clock.tick_start_ns(61) == second + 16'666'666,
           "tick 60 starts at one second exactly");
    const lykoware::FixedStep khz = make(1000);
    expect(khz.tick_start_ns(1'000'000'000'000ULL) == 1'000'000'000'000'000'000LL,
           "start times of very late ticks do not overflow");
}

void test_frame_clock()
{
    lykoware::FrameClock clock;
    expect(clock.lap() >= 0, "a lap is never negative");
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    expect(clock.lap() >= 2 * ms, "a lap measures at least the time slept");
}

}

int main()
{
    test_config_validation();
    test_exact_rate();
    test_frame_rate_independence();
    test_alpha();
    test_limits();
    test_tick_times();
    test_frame_clock();
    if (g_failures != 0) {
        std::cerr << g_failures << " clock assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "clock passed\n";
    return EXIT_SUCCESS;
}
