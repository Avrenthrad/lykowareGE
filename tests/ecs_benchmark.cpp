// ECS iteration benchmark (ADR 0023). Moves ENTITIES entities by their velocity once per frame
// for FRAMES frames, the way a movement system would, and reports the time per frame.
//
// Provisional and CPU-only, like the other perf.* checks (docs/PERFORMANCE.md): the numbers are
// for the pinned hosted runners windows-2022 and ubuntu-24.04, not the owner's RTX 4070 PC, and
// the limit is wide on purpose.
//
// Usage: lykoware_ecs_benchmark ENTITIES FRAMES MEAN_FRAME_LIMIT_MS

#include <lykoware/clock.hpp>
#include <lykoware/ecs.hpp>

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <system_error>

namespace {

namespace ecs = lykoware::ecs;

constexpr std::string_view scope = "provisional CPU-only; pinned hosted runners windows-2022 and ubuntu-24.04";

struct Position {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct Velocity {
    float x = 0.F;
    float y = 0.F;
    float z = 0.F;
};

struct Health {
    int value = 100;
};

bool parse(std::string_view text, std::int64_t& value)
{
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size() && value > 0;
}

int run(std::int64_t count, std::int64_t frames, std::int64_t limit_ms)
{
    ecs::World world;
    for (std::int64_t i = 0; i < count; ++i) {
        const ecs::Entity e = world.create();
        world.add<Position>(e, Position{static_cast<double>(i), 0.0, 0.0});
        world.add<Velocity>(e, Velocity{1.F, 0.5F, static_cast<float>(i % 7)});
        // Every third entity also has health, so the storages differ in size and order.
        if (i % 3 == 0) {
            world.add<Health>(e);
        }
    }

    const float dt = 1.F / 60.F;
    lykoware::FrameClock timer;
    (void)timer.lap();
    for (std::int64_t frame = 0; frame < frames; ++frame) {
        world.each<Position, Velocity>([dt](ecs::Entity, Position& p, const Velocity& v) {
            p.x += static_cast<double>(v.x * dt);
            p.y += static_cast<double>(v.y * dt);
            p.z += static_cast<double>(v.z * dt);
        });
    }
    const double total_ms = static_cast<double>(timer.lap()) / 1e6;
    const double mean = total_ms / static_cast<double>(frames);

    // Check the work was done (and not optimised away): entity 0 moved frames * dt along y.
    double moved = 0.0;
    world.each<Position>([&moved](ecs::Entity e, Position& p) {
        if (e.index == 0) {
            moved = p.y;
        }
    });
    const double expected = static_cast<double>(frames) * static_cast<double>(0.5F * dt);
    const bool correct = moved > expected * 0.999 && moved < expected * 1.001;
    const bool within = mean <= static_cast<double>(limit_ms);

    char line[512];
    std::snprintf(line, sizeof(line),
                  "ecs-iterate: %lld entities, %lld frames: mean %.4f ms per frame (%.2f ns per entity); %.*s; %s "
                  "(limit %lld ms mean per frame)",
                  static_cast<long long>(count), static_cast<long long>(frames), mean,
                  mean * 1e6 / static_cast<double>(count), static_cast<int>(scope.size()), scope.data(),
                  within ? "within" : "over", static_cast<long long>(limit_ms));
    std::cout << line << '\n';
    if (!correct) {
        std::cerr << "the movement result is wrong: " << moved << " instead of " << expected << '\n';
        return EXIT_FAILURE;
    }
    return within ? EXIT_SUCCESS : EXIT_FAILURE;
}

}

int main(int argc, char* argv[])
{
    std::int64_t count = 0;
    std::int64_t frames = 0;
    std::int64_t limit_ms = 0;
    if (argc != 4 || !parse(argv[1], count) || !parse(argv[2], frames) || !parse(argv[3], limit_ms)
        || count > 10'000'000) {
        std::cerr << "usage: lykoware_ecs_benchmark ENTITIES FRAMES MEAN_FRAME_LIMIT_MS (all positive)\n";
        return EXIT_FAILURE;
    }
    return run(count, frames, limit_ms);
}
