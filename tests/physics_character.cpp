// physics.character: characters walking on height-field terrain (ADR 0020, physics spike 3).
// Links only lykoware::engine. Every case runs fixed 60 Hz ticks (ADR 0017), so results are
// deterministic; the last case checks a recorded state hash.

#include <lykoware/clock.hpp>
#include <lykoware/physics.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
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

bool close_to(float a, float b, float tolerance)
{
    return std::fabs(a - b) <= tolerance;
}

std::string hex(std::uint64_t value)
{
    char text[19];
    std::snprintf(text, sizeof(text), "0x%016llx", static_cast<unsigned long long>(value));
    return text;
}

const char* name(physics::GroundState state)
{
    switch (state) {
    case physics::GroundState::OnGround:
        return "OnGround";
    case physics::GroundState::OnSteepGround:
        return "OnSteepGround";
    case physics::GroundState::NotSupported:
        return "NotSupported";
    case physics::GroundState::InAir:
        break;
    }
    return "InAir";
}

float tick_seconds()
{
    std::string error;
    const auto clock = lykoware::FixedStep::create({}, error);
    return clock ? static_cast<float>(clock->tick_seconds()) : 1.F / 60.F;
}

void steps(physics::World& world, int count)
{
    bool ok = true;
    for (int i = 0; i < count; ++i) {
        ok = world.step(tick_seconds()) && ok;
    }
    expect(ok, "every step succeeds");
}

std::unique_ptr<physics::World> make_world(std::uint32_t workers = 0)
{
    physics::WorldConfig config;
    config.worker_threads = workers;
    std::string error;
    auto world = physics::World::create(config, error);
    if (!world) {
        std::cerr << "FATAL: world: " << error << '\n';
        std::exit(EXIT_FAILURE);
    }
    return world;
}

// A 32.5 x 32.5 m terrain from x, z = -16 to 16.5 (66 x 66 samples, 0.5 m apart) whose height at
// world x is `profile(x)`.
physics::BodyId add_terrain(physics::World& world, const std::function<float(float)>& profile)
{
    constexpr std::uint32_t samples = 66;
    constexpr float spacing = 0.5F;
    physics::HeightFieldShape field;
    field.samples_per_side = samples;
    field.spacing = spacing;
    field.heights.resize(static_cast<std::size_t>(samples) * samples);
    for (std::uint32_t z = 0; z < samples; ++z) {
        for (std::uint32_t x = 0; x < samples; ++x) {
            field.heights[z * samples + x] = profile(-16.F + static_cast<float>(x) * spacing);
        }
    }
    physics::BodyDesc terrain;
    terrain.shape = std::move(field);
    terrain.motion = physics::MotionType::Static;
    terrain.position = {-16.F, 0.F, -16.F};
    std::string error;
    const auto body = world.create_body(terrain, error);
    if (!body) {
        std::cerr << "FATAL: terrain: " << error << '\n';
        std::exit(EXIT_FAILURE);
    }
    return *body;
}

physics::CharacterId add_character(physics::World& world, physics::Vec3 feet)
{
    physics::CharacterDesc desc;
    desc.position = feet;
    std::string error;
    const auto character = world.create_character(desc, error);
    if (!character) {
        std::cerr << "FATAL: character: " << error << '\n';
        std::exit(EXIT_FAILURE);
    }
    return *character;
}

physics::CharacterState state_of(const physics::World& world, physics::CharacterId character)
{
    const auto state = world.character_state(character);
    if (!state) {
        std::cerr << "FATAL: character state missing\n";
        std::exit(EXIT_FAILURE);
    }
    return *state;
}

void test_land_and_walk()
{
    auto world = make_world();
    const physics::BodyId terrain = add_terrain(*world, [](float) { return 0.F; });
    const physics::CharacterId hero = add_character(*world, {0.F, 2.F, 0.F});
    expect(world->character_count() == 1, "one character exists");

    steps(*world, 90);
    physics::CharacterState state = state_of(*world, hero);
    expect(state.ground == physics::GroundState::OnGround, std::string{"it lands on the ground, not "} + name(state.ground));
    expect(close_to(state.position.y, 0.F, 0.05F), "its feet rest on the terrain");
    expect(state.ground_body && *state.ground_body == terrain, "it reports the terrain as its ground");
    expect(close_to(state.ground_normal.y, 1.F, 1e-3F), "flat ground's normal points up");

    const auto hit = world->cast_ray({5.F, 10.F, 5.F}, {0.F, -20.F, 0.F});
    expect(hit && hit->body == terrain && close_to(hit->point.y, 0.F, 0.02F), "rays hit the height field");

    expect(world->set_character_input(hero, {2.F, 0.F, 0.F}), "the character takes input");
    steps(*world, 60);
    state = state_of(*world, hero);
    expect(close_to(state.position.x, 2.F, 0.1F), "it walks two metres in a second at 2 m/s");
    expect(close_to(state.position.y, 0.F, 0.05F) && state.ground == physics::GroundState::OnGround,
           "it stays on the ground while walking");

    expect(world->set_character_input(hero, {0.F, 0.F, 0.F}, 5.F), "a jump is accepted");
    steps(*world, 15);
    state = state_of(*world, hero);
    expect(state.position.y > 0.8F && state.ground == physics::GroundState::InAir, "a 5 m/s jump leaves the ground");
    steps(*world, 60);
    state = state_of(*world, hero);
    expect(state.ground == physics::GroundState::OnGround && close_to(state.position.y, 0.F, 0.05F),
           "it lands again, and the jump is used only once");
}

void test_slopes()
{
    // Flat for x < 0, then a 15 degree slope (walkable) up to x = 6, then 63 degrees (too steep).
    auto world = make_world();
    add_terrain(*world, [](float x) {
        if (x < 0.F) {
            return 0.F;
        }
        if (x < 6.F) {
            return 0.268F * x;
        }
        return 0.268F * 6.F + 2.F * (x - 6.F);
    });
    const physics::CharacterId hero = add_character(*world, {-3.F, 0.5F, 0.F});
    steps(*world, 30);
    expect(world->set_character_input(hero, {2.F, 0.F, 0.F}), "the character takes input");

    steps(*world, 120); // two seconds: from x = -3 onto the gentle slope
    physics::CharacterState state = state_of(*world, hero);
    expect(state.position.x > 0.5F && state.position.x < 6.F, "it walks onto the gentle slope");
    expect(close_to(state.position.y, 0.268F * state.position.x, 0.15F), "it follows the 15 degree slope up");
    expect(state.ground == physics::GroundState::OnGround, std::string{"the 15 degree slope is walkable, not "} + name(state.ground));

    // Six more seconds: it reaches the steep part after about three and pushes into it for the
    // rest. Allowed to climb 63 degrees it would be metres up by then (checked by breaking
    // max_slope); held back, it stays at the foot (x = 6, height 1.61 m).
    steps(*world, 360);
    state = state_of(*world, hero);
    expect(state.position.x < 6.1F && state.position.y < 1.8F,
           "it cannot climb the 63 degree slope and stays at its foot");
}

void test_steps()
{
    // Two ledges across the walk: 0.3 m (under the 0.4 m step height) and 0.7 m (over it).
    for (const float ledge : {0.3F, 0.7F}) {
        auto world = make_world();
        add_terrain(*world, [](float) { return 0.F; });
        physics::BodyDesc block;
        block.shape = physics::BoxShape{{5.F, ledge * 0.5F, 5.F}};
        block.motion = physics::MotionType::Static;
        block.position = {8.F, ledge * 0.5F, 0.F}; // from x = 3 to 13
        std::string error;
        expect(world->create_body(block, error).has_value(), "the ledge is created");
        const physics::CharacterId hero = add_character(*world, {0.F, 0.1F, 0.F});
        steps(*world, 30);
        expect(world->set_character_input(hero, {2.F, 0.F, 0.F}), "the character takes input");
        steps(*world, 180);
        const physics::CharacterState state = state_of(*world, hero);
        if (ledge < 0.4F) {
            expect(state.position.x > 4.F && close_to(state.position.y, ledge, 0.05F),
                   "it walks up a 0.3 m ledge without jumping");
        } else {
            expect(state.position.x < 3.F && close_to(state.position.y, 0.F, 0.05F), "a 0.7 m ledge blocks it");
        }
    }
}

void test_misuse()
{
    auto world = make_world();
    add_terrain(*world, [](float) { return 0.F; });
    const physics::CharacterId none{};
    expect(!world->is_valid(none) && !world->character_state(none), "the default character handle is invalid");
    expect(!world->set_character_input(none, {1.F, 0.F, 0.F}) && !world->destroy_character(none),
           "the default character handle does nothing");

    const physics::CharacterId hero = add_character(*world, {0.F, 1.F, 0.F});
    expect(!world->set_character_input(hero, {std::nanf(""), 0.F, 0.F}), "non-finite input is refused");
    expect(!world->set_character_input(hero, {}, -1.F), "a negative jump is refused");
    expect(world->destroy_character(hero) && !world->is_valid(hero), "a character is destroyed");
    expect(!world->destroy_character(hero) && !world->character_state(hero), "a destroyed handle does nothing");
    const physics::CharacterId next = add_character(*world, {0.F, 1.F, 0.F});
    expect(next.index == hero.index && next.generation != hero.generation && !world->is_valid(hero),
           "a reused character slot gets a new generation");
    steps(*world, 2);

    std::string error;
    const auto refused = [&](physics::CharacterDesc desc, std::string_view what) {
        expect(!world->create_character(desc, error) && !error.empty(), what);
    };
    physics::CharacterDesc bad;
    bad.radius = 0.F;
    refused(bad, "a zero radius is refused");
    bad = {};
    bad.cylinder_half_height = -1.F;
    refused(bad, "a negative height is refused");
    bad = {};
    bad.max_slope = 2.F;
    refused(bad, "a slope of 2 radians is refused");
    bad = {};
    bad.step_height = -0.1F;
    refused(bad, "a negative step height is refused");
    bad = {};
    bad.mass = 0.F;
    refused(bad, "a zero mass is refused");
    expect(world->character_count() == 1, "refused characters are not created");

    const auto field_refused = [&](physics::HeightFieldShape field, physics::MotionType motion, std::string_view what) {
        physics::BodyDesc desc;
        desc.shape = std::move(field);
        desc.motion = motion;
        expect(!world->create_body(desc, error) && !error.empty(), what);
    };
    physics::HeightFieldShape field;
    field.samples_per_side = 4;
    field.heights.assign(16, 0.F);
    field_refused(field, physics::MotionType::Dynamic, "a dynamic height field is refused");
    physics::HeightFieldShape wrong = field;
    wrong.heights.resize(15);
    field_refused(wrong, physics::MotionType::Static, "a height field with the wrong sample count is refused");
    wrong = field;
    wrong.samples_per_side = 3;
    wrong.heights.assign(9, 0.F);
    field_refused(wrong, physics::MotionType::Static, "an odd or tiny height field is refused");
    wrong = field;
    wrong.heights[5] = std::nanf("");
    field_refused(wrong, physics::MotionType::Static, "a non-finite height is refused");
    wrong = field;
    wrong.spacing = 0.F;
    field_refused(wrong, physics::MotionType::Static, "zero spacing is refused");
}

// --- determinism --------------------------------------------------------------------------

constexpr std::uint64_t expected_hash = 0xbe1264481a8a8720ULL;

std::uint64_t run_scene(std::uint32_t workers)
{
    auto world = make_world(workers);
    // Rolling ground as a triangle wave (1 m high, 4 m long), built only from operations that
    // round identically everywhere: std::sin differs between platforms' maths libraries.
    add_terrain(*world, [](float x) {
        const float t = x * 0.25F;
        const float phase = t - std::floor(t);
        return 2.F * (phase < 0.5F ? phase : 1.F - phase);
    });
    for (int i = 0; i < 3; ++i) {
        const physics::CharacterId walker = add_character(*world, {static_cast<float>(i) * 2.F - 2.F, 1.5F, -4.F});
        world->set_character_input(walker, {0.F, 0.F, 1.5F + 0.5F * static_cast<float>(i)}, i == 1 ? 4.F : 0.F);
    }
    for (int i = 0; i < 6; ++i) {
        physics::BodyDesc crate;
        crate.shape = physics::BoxShape{{0.3F, 0.3F, 0.3F}};
        crate.position = {static_cast<float>(i) - 2.5F, 2.F, 2.F};
        crate.angular_velocity = {0.F, 0.3F * static_cast<float>(i), 0.F};
        std::string error;
        expect(world->create_body(crate, error).has_value(), "a crate is created");
    }
    steps(*world, 240);
    return world->state_hash();
}

void test_determinism()
{
    const std::uint64_t one = run_scene(1);
    const std::uint64_t four = run_scene(4);
    std::cout << "physics.character state hash after 240 ticks: " << hex(one) << '\n';
    expect(one == four, "one and four worker threads give the same state");
    expect(one == expected_hash, "the state hash matches the value every platform must produce (" + hex(expected_hash) + ")");
}

}

int main()
{
    test_land_and_walk();
    test_slopes();
    test_steps();
    test_misuse();
    test_determinism();
    if (g_failures != 0) {
        std::cerr << g_failures << " physics character assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "physics character passed\n";
    return EXIT_SUCCESS;
}
