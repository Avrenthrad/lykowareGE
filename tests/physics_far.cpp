// physics.far_from_origin: the same scene 16 km and 64 km from the origin behaves like the
// scene at the origin (ADR 0021: one world of 16 km or more; ADR 0022: double-precision
// positions). Links only lykoware::engine.
//
// The scene: flat height-field terrain, a sphere dropped onto it, a kinematic box creeping at
// 1 mm/s, and a character that walks for one second. In single precision the creeping box
// could not move at 16 km: each tick's 0.017 mm is less than half the 0.98 mm gap between
// floats there, so it would round away.

#include <lykoware/clock.hpp>
#include <lykoware/physics.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

namespace {

namespace physics = lykoware::physics;

int g_failures = 0;

void expect(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

bool close_to(double a, double b, double tolerance)
{
    return std::fabs(a - b) <= tolerance;
}

std::string hex(std::uint64_t value)
{
    char text[19];
    std::snprintf(text, sizeof(text), "0x%016llx", static_cast<unsigned long long>(value));
    return text;
}

float tick_seconds()
{
    std::string error;
    const auto clock = lykoware::FixedStep::create({}, error);
    return clock ? static_cast<float>(clock->tick_seconds()) : 1.F / 60.F;
}

physics::DVec3 offset_by(physics::DVec3 offset, double x, double y, double z)
{
    return {offset.x + x, offset.y + y, offset.z + z};
}

[[noreturn]] void fatal(std::string_view what, std::string_view error)
{
    std::cerr << "FATAL: " << what << ": " << error << '\n';
    std::exit(EXIT_FAILURE);
}

// What the scene ends as, relative to its offset.
struct Result {
    double ball_y = 0.0;     // sphere centre above the terrain
    double ball_speed = 0.0; // sphere speed at the end
    double creep = 0.0;      // distance the creeping box moved
    physics::DVec3 feet;     // character feet
    physics::DVec3 ray_point;
    std::uint64_t hash = 0;
    float ray_fraction = 0.F; // downward ray onto the sphere's top
    bool on_ground = false;
};

Result run(physics::DVec3 offset, std::uint32_t workers)
{
    physics::WorldConfig config;
    config.worker_threads = workers;
    std::string error;
    auto world = physics::World::create(config, error);
    if (!world) {
        fatal("world", error);
    }

    // Flat terrain from -8 to 8.5 m on x and z (34 x 34 samples, 0.5 m apart).
    physics::HeightFieldShape field;
    field.samples_per_side = 34;
    field.spacing = 0.5F;
    field.heights.assign(34U * 34U, 0.F);
    physics::BodyDesc terrain;
    terrain.shape = std::move(field);
    terrain.motion = physics::MotionType::Static;
    terrain.position = offset_by(offset, -8.0, 0.0, -8.0);
    if (!world->create_body(terrain, error)) {
        fatal("terrain", error);
    }

    physics::BodyDesc ball;
    ball.shape = physics::SphereShape{0.5F};
    ball.position = offset_by(offset, 3.0, 3.0, 0.0);
    const auto ball_id = world->create_body(ball, error);
    if (!ball_id) {
        fatal("ball", error);
    }

    physics::BodyDesc creeper;
    creeper.shape = physics::BoxShape{{0.25F, 0.25F, 0.25F}};
    creeper.motion = physics::MotionType::Kinematic;
    creeper.position = offset_by(offset, -3.0, 1.0, -3.0);
    creeper.linear_velocity = {0.001F, 0.F, 0.F};
    const auto creeper_id = world->create_body(creeper, error);
    if (!creeper_id) {
        fatal("creeper", error);
    }

    physics::CharacterDesc walker_desc;
    walker_desc.position = offset_by(offset, -4.0, 0.2, 3.0);
    const auto walker = world->create_character(walker_desc, error);
    if (!walker) {
        fatal("character", error);
    }

    // One second walking at 2 m/s along +x, then one second standing.
    bool stepped = true;
    world->set_character_input(*walker, {2.F, 0.F, 0.F});
    for (int i = 0; i < 60; ++i) {
        stepped = world->step(tick_seconds()) && stepped;
    }
    world->set_character_input(*walker, {0.F, 0.F, 0.F});
    for (int i = 0; i < 60; ++i) {
        stepped = world->step(tick_seconds()) && stepped;
    }
    expect(stepped, "every step succeeds");

    const auto ball_state = world->state(*ball_id);
    const auto creeper_state = world->state(*creeper_id);
    const auto walker_state = world->character_state(*walker);
    if (!ball_state || !creeper_state || !walker_state) {
        fatal("state", "missing");
    }
    Result result;
    result.ball_y = ball_state->position.y - offset.y;
    const physics::Vec3 v = ball_state->linear_velocity;
    result.ball_speed = std::sqrt(static_cast<double>(v.x * v.x + v.y * v.y + v.z * v.z));
    result.creep = creeper_state->position.x - (offset.x - 3.0);
    result.feet = {walker_state->position.x - offset.x, walker_state->position.y - offset.y,
                   walker_state->position.z - offset.z};
    result.on_ground = walker_state->ground == physics::GroundState::OnGround;

    const auto hit = world->cast_ray(offset_by(offset, 3.0, 10.0, 0.0), {0.F, -20.F, 0.F});
    if (hit && hit->body == *ball_id) {
        result.ray_fraction = hit->fraction;
        result.ray_point = {hit->point.x - offset.x, hit->point.y - offset.y, hit->point.z - offset.z};
    }
    result.hash = world->state_hash();
    return result;
}

double distance(physics::DVec3 a, physics::DVec3 b)
{
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    const double dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Recorded state hash of the 16 km scene; every build must produce it (ADR 0018, ADR 0022).
constexpr std::uint64_t expected_far_hash = 0x93590957722d8131ULL;

struct Place {
    const char* name;
    physics::DVec3 offset;
};

}

int main()
{
    const Result home = run({0.0, 0.0, 0.0}, 0);
    const Place places[] = {
        {"origin", {0.0, 0.0, 0.0}},
        {"16 km", {16000.0, 0.0, -16000.0}},
        {"64 km", {64000.0, 120.0, 64000.0}},
    };
    for (const Place& place : places) {
        const Result far = run(place.offset, 0);
        const std::string at = std::string(" at ") + place.name;
        std::cout << place.name << ": ball y " << far.ball_y << ", creep " << far.creep * 1000.0 << " mm, feet ("
                  << far.feet.x << ", " << far.feet.y << ", " << far.feet.z << "), ray " << far.ray_fraction
                  << "; differs from the origin scene by " << distance(far.feet, home.feet) * 1000.0
                  << " mm (feet) and " << std::fabs(far.ball_y - home.ball_y) * 1000.0 << " mm (ball)\n";

        // Resting contacts sink up to Jolt's 2 cm penetration slop.
        expect(close_to(far.ball_y, 0.49, 0.015) && far.ball_speed < 0.01, "the sphere rests on the terrain" + at);
        expect(close_to(far.creep, 0.002, 1e-7), "the 1 mm/s box moves 2 mm in 2 s" + at);
        expect(far.on_ground && close_to(far.feet.y, 0.0, 0.01), "the character stands on the terrain" + at);
        expect(close_to(far.feet.x, -2.0, 0.05) && close_to(far.feet.z, 3.0, 1e-4),
               "the character walks 2 m in 1 s at 2 m/s" + at);
        expect(close_to(far.ray_fraction, 0.45, 0.002) && close_to(far.ray_point.y, 0.99, 0.03),
               "a ray hits the top of the sphere" + at);
        expect(distance(far.feet, home.feet) < 1e-6 && std::fabs(far.ball_y - home.ball_y) < 1e-6
                   && std::fabs(far.creep - home.creep) < 1e-9,
               "the scene ends within a micrometre of the scene at the origin" + at);
    }

    // Determinism far from the origin: 1 and 4 worker threads agree, and every build must
    // produce the recorded hash.
    const Result one = run(places[1].offset, 1);
    const Result four = run(places[1].offset, 4);
    std::cout << "physics.far_from_origin state hash at 16 km after 120 ticks: " << hex(one.hash) << '\n';
    expect(one.hash == four.hash, "1 and 4 worker threads give the same state at 16 km");
    expect(one.hash == expected_far_hash,
           "the 16 km state hash matches the value every platform must produce (" + hex(expected_far_hash) + ")");

    if (g_failures != 0) {
        std::cerr << g_failures << " far-from-origin assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "physics far from origin passed\n";
    return EXIT_SUCCESS;
}
