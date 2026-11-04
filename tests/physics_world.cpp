// physics.world: the Lykoware physics API (ADR 0019) over vendored Jolt. This test links only
// lykoware::engine, so it also proves the public API needs no Jolt types.

#include <lykoware/clock.hpp>
#include <lykoware/physics.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

bool close_to(float a, float b, float tolerance)
{
    return std::fabs(a - b) <= tolerance;
}

float length(physics::Vec3 v)
{
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
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

std::unique_ptr<physics::World> make_world(physics::WorldConfig config = {})
{
    std::string error;
    auto world = physics::World::create(config, error);
    if (!world) {
        std::cerr << "FATAL: world: " << error << '\n';
        std::exit(EXIT_FAILURE);
    }
    return world;
}

physics::BodyId add(physics::World& world, const physics::BodyDesc& desc)
{
    std::string error;
    const auto body = world.create_body(desc, error);
    if (!body) {
        std::cerr << "FATAL: body: " << error << '\n';
        std::exit(EXIT_FAILURE);
    }
    return *body;
}

physics::BodyId add_ground(physics::World& world)
{
    physics::BodyDesc ground;
    ground.shape = physics::BoxShape{{20.F, 0.5F, 20.F}};
    ground.motion = physics::MotionType::Static;
    ground.position = {0.F, -0.5F, 0.F}; // top face at y = 0
    return add(world, ground);
}

void steps(physics::World& world, int count)
{
    bool ok = true;
    for (int i = 0; i < count; ++i) {
        ok = world.step(tick_seconds()) && ok;
    }
    expect(ok, "every step succeeds");
}

void test_config()
{
    std::string error;
    physics::WorldConfig bad;
    bad.max_bodies = 0;
    expect(!physics::World::create(bad, error) && !error.empty(), "max_bodies 0 is refused");
    bad = {};
    bad.step_memory_bytes = 1024;
    expect(!physics::World::create(bad, error) && !error.empty(), "too little step memory is refused");
    bad = {};
    bad.gravity.y = std::nanf("");
    expect(!physics::World::create(bad, error) && !error.empty(), "non-finite gravity is refused");
}

void test_rest_and_rays()
{
    auto world = make_world();
    const physics::BodyId ground = add_ground(*world);
    physics::BodyDesc ball;
    ball.shape = physics::SphereShape{0.5F};
    ball.position = {0.F, 3.F, 0.F};
    const physics::BodyId sphere = add(*world, ball);
    expect(world->body_count() == 2, "two bodies are alive");

    steps(*world, 180); // three seconds at 60 Hz
    const auto rest = world->state(sphere);
    expect(rest.has_value(), "the sphere has a state");
    if (rest) {
        expect(close_to(rest->position.y, 0.5F, 0.05F), "the sphere comes to rest on the ground");
        expect(length(rest->linear_velocity) < 0.05F, "the resting sphere is still");
    }

    const auto down = world->cast_ray({0.F, 10.F, 0.F}, {0.F, -20.F, 0.F});
    expect(down && down->body == sphere, "a ray straight down hits the sphere first");
    if (down) {
        expect(close_to(down->point.y, 1.F, 0.06F), "it hits the top of the sphere");
        expect(close_to(down->fraction, 0.45F, 0.005F), "at 0.45 of the ray's length");
    }
    const auto beside = world->cast_ray({5.F, 10.F, 0.F}, {0.F, -20.F, 0.F});
    expect(beside && beside->body == ground && close_to(beside->point.y, 0.F, 1e-3F), "a ray beside it hits the ground");
    expect(!world->cast_ray({0.F, 10.F, 0.F}, {0.F, 5.F, 0.F}), "a ray pointing away hits nothing");
    expect(!world->cast_ray({0.F, 10.F, 0.F}, {0.F, 0.F, 0.F}), "a zero-length ray hits nothing");

    // A kinematic body moves exactly by its velocity.
    physics::BodyDesc mover;
    mover.motion = physics::MotionType::Kinematic;
    mover.position = {-10.F, 5.F, 0.F};
    mover.linear_velocity = {1.F, 0.F, 0.F};
    const physics::BodyId kinematic = add(*world, mover);
    steps(*world, 60);
    const auto moved = world->state(kinematic);
    expect(moved && close_to(moved->position.x, -9.F, 1e-3F) && close_to(moved->position.y, 5.F, 1e-5F),
           "a kinematic body moves one metre in one second at 1 m/s, ignoring gravity");
}

void test_invalid_handles()
{
    auto world = make_world();
    const physics::BodyId ground = add_ground(*world);
    const physics::BodyId none{};
    expect(!world->is_valid(none), "the default handle is invalid");
    expect(!world->state(none), "the default handle has no state");
    expect(!world->destroy_body(none), "the default handle cannot be destroyed");
    expect(!world->set_velocity(none, {1.F, 0.F, 0.F}, {}), "the default handle cannot be moved");
    expect(!world->set_velocity(ground, {1.F, 0.F, 0.F}, {}), "a static body cannot be given a velocity");
    expect(!world->is_valid(physics::BodyId{999, 1}), "an index out of range is invalid");
    expect(!world->is_valid(physics::BodyId{ground.index, ground.generation + 1}), "a wrong generation is invalid");

    physics::BodyDesc box;
    box.position = {0.F, 2.F, 0.F};
    const physics::BodyId first = add(*world, box);
    expect(world->set_velocity(first, {0.F, 1.F, 0.F}, {}), "a dynamic body takes a velocity");
    expect(world->destroy_body(first), "a live body is destroyed");
    expect(!world->is_valid(first), "a destroyed handle is invalid");
    expect(!world->state(first), "a destroyed handle has no state");
    expect(!world->destroy_body(first), "a body cannot be destroyed twice");
    expect(!world->set_velocity(first, {1.F, 0.F, 0.F}, {}), "a destroyed handle cannot be moved");

    const physics::BodyId second = add(*world, box);
    expect(second.index == first.index && second.generation != first.generation,
           "a freed slot is reused with a new generation");
    expect(world->is_valid(second) && !world->is_valid(first), "the old handle never reaches the new body");
    const auto hit = world->cast_ray({0.F, 10.F, 0.F}, {0.F, -20.F, 0.F});
    expect(hit && hit->body == second, "ray hits report the new handle");

    std::string error;
    physics::BodyDesc bad;
    bad.shape = physics::SphereShape{-1.F};
    expect(!world->create_body(bad, error) && !error.empty(), "a negative radius is refused");
    bad.shape = physics::BoxShape{{1.F, 0.F, 1.F}};
    expect(!world->create_body(bad, error) && !error.empty(), "a flat box is refused");
    bad.shape = physics::CapsuleShape{0.F, 0.5F};
    expect(!world->create_body(bad, error) && !error.empty(), "a capsule without a cylinder is refused");
    bad.shape = physics::BoxShape{};
    bad.rotation = {0.F, 0.F, 0.F, 0.F};
    expect(!world->create_body(bad, error) && !error.empty(), "a zero rotation is refused");
    expect(world->body_count() == 2, "refused bodies are not created");
}

void test_limits_and_cleanup()
{
    physics::WorldConfig small;
    small.max_bodies = 4;
    auto world = make_world(small);
    std::vector<physics::BodyId> bodies;
    for (int i = 0; i < 4; ++i) {
        physics::BodyDesc box;
        box.position = {static_cast<float>(i) * 2.F, 1.F, 0.F};
        bodies.push_back(add(*world, box));
    }
    std::string error;
    expect(!world->create_body({}, error) && !error.empty(), "a fifth body is refused at max_bodies 4");
    expect(world->destroy_body(bodies.back()), "destroying one frees a place");
    expect(world->create_body({}, error).has_value(), "then a body can be created again");

    // Repeated create, step and destroy leaves nothing behind.
    auto big = make_world();
    const physics::BodyId ground = add_ground(*big);
    for (int round = 0; round < 5; ++round) {
        std::vector<physics::BodyId> boxes;
        for (int i = 0; i < 200; ++i) {
            physics::BodyDesc box;
            box.position = {static_cast<float>(i % 20) - 10.F, 1.F + static_cast<float>(i / 20), 0.F};
            boxes.push_back(add(*big, box));
        }
        steps(*big, 10);
        bool destroyed = true;
        for (const physics::BodyId box : boxes) {
            destroyed = big->destroy_body(box) && destroyed;
        }
        expect(destroyed && big->body_count() == 1, "each round destroys all its bodies");
    }
    expect(big->is_valid(ground), "the ground survives the rounds");

    // Worlds can be created and destroyed repeatedly, and several can be alive at once.
    for (int i = 0; i < 3; ++i) {
        auto again = make_world();
        add_ground(*again);
        steps(*again, 2);
    }
}

// --- determinism --------------------------------------------------------------------------

constexpr std::uint64_t expected_hash = 0x84f90a3cbf37a28bULL;

void build_scene(physics::World& world)
{
    add_ground(world);
    for (int x = 0; x < 3; ++x) {
        for (int y = 0; y < 3; ++y) {
            for (int z = 0; z < 3; ++z) {
                physics::BodyDesc body;
                switch ((x + y + z) % 3) {
                case 0:
                    body.shape = physics::BoxShape{{0.4F, 0.3F, 0.5F}};
                    break;
                case 1:
                    body.shape = physics::SphereShape{0.45F};
                    break;
                default:
                    body.shape = physics::CapsuleShape{0.3F, 0.3F};
                    break;
                }
                body.position = {(static_cast<float>(x) - 1.F) * 1.2F, 1.5F + static_cast<float>(y) * 1.3F,
                                 (static_cast<float>(z) - 1.F) * 1.2F};
                body.rotation = {0.F, 0.38268343F, 0.F, 0.92387953F}; // 45 degrees about +Y
                body.angular_velocity = {0.2F * static_cast<float>(x), 0.F, -0.1F * static_cast<float>(z)};
                add(world, body);
            }
        }
    }
}

void test_determinism()
{
    physics::WorldConfig one_thread;
    one_thread.worker_threads = 1;
    physics::WorldConfig four_threads;
    four_threads.worker_threads = 4;
    auto a = make_world(one_thread);
    auto b = make_world(four_threads);
    build_scene(*a);
    build_scene(*b);
    steps(*a, 240);
    steps(*b, 240);
    const std::uint64_t hash = a->state_hash();
    std::cout << "physics.world state hash after 240 ticks: " << hex(hash) << '\n';
    expect(hash == b->state_hash(), "one and four worker threads give the same state");
    expect(hash == expected_hash, "the state hash matches the value every platform must produce (" + hex(expected_hash) + ")");
}

}

int main()
{
    test_config();
    test_rest_and_rays();
    test_invalid_handles();
    test_limits_and_cleanup();
    test_determinism();
    if (g_failures != 0) {
        std::cerr << g_failures << " physics world assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "physics world passed\n";
    return EXIT_SUCCESS;
}
