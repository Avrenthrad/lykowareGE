// Physics body-count benchmark (physics spike 4). Steps a pile of dynamic boxes collapsing onto
// the ground at the fixed 60 Hz tick (ADR 0017) and reports per-step wall time.
//
// Provisional and CPU-only, like the other perf.* checks (docs/PERFORMANCE.md). The numbers
// are for the pinned hosted runners windows-2022 and ubuntu-24.04, not an RTX 4070 or MSI Claw
// result, and the ceiling is deliberately wide: it catches large regressions, it is not a
// frame budget.
//
// Usage: lykoware_physics_benchmark BODIES TICKS MEAN_STEP_LIMIT_MS

#include <lykoware/clock.hpp>
#include <lykoware/physics.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

namespace {

namespace physics = lykoware::physics;

constexpr std::string_view scope =
    "provisional CPU-only; pinned hosted runners windows-2022 and ubuntu-24.04";

bool parse(std::string_view text, std::int64_t& value)
{
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size() && value > 0;
}

int run(std::int64_t body_count, std::int64_t ticks, std::int64_t limit_ms)
{
    physics::WorldConfig config;
    config.max_bodies = static_cast<std::uint32_t>(body_count) + 16;
    config.max_body_pairs = static_cast<std::uint32_t>(body_count) * 16;
    config.max_contact_constraints = static_cast<std::uint32_t>(body_count) * 16;
    std::string error;
    auto world = physics::World::create(config, error);
    if (!world) {
        std::cerr << "world: " << error << '\n';
        return EXIT_FAILURE;
    }

    physics::BodyDesc ground;
    ground.shape = physics::BoxShape{{40.F, 0.5F, 40.F}};
    ground.motion = physics::MotionType::Static;
    ground.position = {0.F, -0.5F, 0.F};
    if (!world->create_body(ground, error)) {
        std::cerr << "ground: " << error << '\n';
        return EXIT_FAILURE;
    }

    // Layers of 16 x 16 boxes, 0.6 m apart, each turned a little so the pile collapses.
    std::vector<physics::BodyId> boxes;
    for (std::int64_t i = 0; i < body_count; ++i) {
        const auto column = static_cast<float>(i % 16);
        const auto row = static_cast<float>((i / 16) % 16);
        const auto layer = static_cast<float>(i / 256);
        physics::BodyDesc box;
        box.shape = physics::BoxShape{{0.25F, 0.25F, 0.25F}};
        box.position = {(column - 7.5F) * 0.6F, 1.F + layer * 0.6F, (row - 7.5F) * 0.6F};
        const float turn = 0.05F * static_cast<float>(i % 7);
        box.rotation = {0.F, turn, 0.F, std::sqrt(1.F - turn * turn)};
        box.angular_velocity = {0.1F * static_cast<float>(i % 3), 0.F, 0.1F * static_cast<float>(i % 5)};
        const auto id = world->create_body(box, error);
        if (!id) {
            std::cerr << "box: " << error << '\n';
            return EXIT_FAILURE;
        }
        boxes.push_back(*id);
    }

    std::string clock_error;
    const auto clock = lykoware::FixedStep::create({}, clock_error);
    const auto tick = static_cast<float>(clock ? clock->tick_seconds() : 1.0 / 60.0);
    std::vector<double> step_ms;
    step_ms.reserve(static_cast<std::size_t>(ticks));
    lykoware::FrameClock timer;
    bool stepped = true;
    for (std::int64_t t = 0; t < ticks; ++t) {
        (void)timer.lap();
        stepped = world->step(tick) && stepped;
        step_ms.push_back(static_cast<double>(timer.lap()) / 1e6);
    }

    std::size_t moving = 0;
    bool fell_through = false;
    for (const physics::BodyId id : boxes) {
        const auto state = world->state(id);
        const physics::Vec3 v = state->linear_velocity;
        moving += (v.x * v.x + v.y * v.y + v.z * v.z) > 0.0025F ? 1 : 0;
        fell_through = fell_through || state->position.y < -0.2F;
    }

    double total = 0.0;
    for (const double ms : step_ms) {
        total += ms;
    }
    const double mean = total / static_cast<double>(step_ms.size());
    std::vector<double> sorted = step_ms;
    std::sort(sorted.begin(), sorted.end());
    const double p95 = sorted[static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(sorted.size()))) - 1];
    const double worst = sorted.back();
    const bool within = mean <= static_cast<double>(limit_ms);

    char line[512];
    std::snprintf(line, sizeof(line),
                  "physics-bodies: %lld boxes, %lld ticks at 60 Hz, %u hardware threads: mean %.3f ms, p95 %.3f ms, "
                  "max %.3f ms per step, %zu still moving at the end; %.*s; %s (limit %lld ms mean per step)",
                  static_cast<long long>(body_count), static_cast<long long>(ticks), std::thread::hardware_concurrency(),
                  mean, p95, worst, moving, static_cast<int>(scope.size()), scope.data(), within ? "within" : "over",
                  static_cast<long long>(limit_ms));
    std::cout << line << '\n';
    if (!stepped) {
        std::cerr << "a physics step failed\n";
        return EXIT_FAILURE;
    }
    if (fell_through) {
        std::cerr << "a box fell through the ground\n";
        return EXIT_FAILURE;
    }
    return within ? EXIT_SUCCESS : EXIT_FAILURE;
}

}

int main(int argc, char* argv[])
{
    std::int64_t bodies = 0;
    std::int64_t ticks = 0;
    std::int64_t limit_ms = 0;
    if (argc != 4 || !parse(argv[1], bodies) || !parse(argv[2], ticks) || !parse(argv[3], limit_ms)
        || bodies > 1'000'000) {
        std::cerr << "usage: lykoware_physics_benchmark BODIES TICKS MEAN_STEP_LIMIT_MS (all positive)\n";
        return EXIT_FAILURE;
    }
    return run(bodies, ticks, limit_ms);
}
