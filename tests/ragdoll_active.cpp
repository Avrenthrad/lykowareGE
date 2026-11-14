// physics.ragdoll_active (ADR 0081): an active ragdoll. Motors pull the training mannequin's bodies
// toward its standing pose. A fixed-seed shove makes it stumble and recover; a harder one makes it
// fall, lie limp, rise and recover. The test fails if the body never leaves the animated pose, and
// fails if it never returns.

#include "ragdoll_blend.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <random>
#include <string>

namespace {
namespace physics = lykoware::physics;
namespace ragdoll = lykoware::ragdoll;
namespace animation = lykoware::animation;
namespace math = lykoware::math;

int failures = 0;
void check(bool ok, const std::string& why)
{
    if (!ok) {
        std::cerr << "FAIL: " << why << '\n';
        ++failures;
    }
}

constexpr std::uint32_t hz = ragdoll_blend::ticks_per_second;
constexpr float dt = 1.F / static_cast<float>(hz);
constexpr std::uint32_t seed = 213; // the shove's direction; std::mt19937's raw output is the same everywhere
constexpr std::uint64_t shove_tick = hz;   // 1 s of standing first
constexpr std::uint64_t shove_ticks = 12;  // a shove lasts 0.2 s
constexpr std::uint64_t run_ticks = 8 * hz;

ragdoll::ActiveProfile test_profile()
{
    ragdoll::ActiveProfile p;
    p.strength = .1F;
    return p;
}

struct Trace {
    bool made = false;
    bool stumbled = false;
    bool fell = false;
    bool rose = false;
    std::optional<std::uint64_t> left;     // first tick out of Animated
    std::optional<std::uint64_t> returned; // first tick back in Animated after leaving
    float worst_angle = 0.F;
    float worst_offset = 0.F;
    float final_angle = 0.F;
    float final_offset = 0.F;
    ragdoll::ActiveState final_state = ragdoll::ActiveState::Animated;
    std::uint64_t hash = 0;
};

// Stands the mannequin for run_ticks with motors on, and shoves every body at `speed` (m/s,
// horizontal, seeded direction) for shove_ticks, after the motors' follow and before the step.
Trace run(float speed, const ragdoll::ActiveProfile& profile)
{
    Trace trace;
    std::string error;
    auto world = physics::World::create({}, error);
    if (!world) {
        return trace;
    }
    physics::BodyDesc ground;
    ground.shape = physics::BoxShape{{20.F, 1.F, 20.F}};
    ground.motion = physics::MotionType::Static;
    ground.position = {0.0, -1.0, 0.0};
    if (!world->create_body(ground, error)) {
        return trace;
    }
    const auto asset = training_mannequin::make();
    animation::Pose pose = animation::rest_pose(asset.skeleton);
    auto desc = ragdoll_blend::mannequin_bones();
    desc.active = profile;
    auto doll = ragdoll::Ragdoll::create(*world, asset.skeleton, pose, {{0.0, .1, 0.0}, {}, 1.F}, desc, error);
    auto active = ragdoll::ActiveRagdoll::create(desc.active, error);
    if (!doll || !active) {
        std::cerr << error << '\n';
        return trace;
    }
    std::mt19937 random(seed);
    const float heading = static_cast<float>(random() % 360U) * 3.14159265F / 180.F;
    const math::Vec3 push{speed * std::cos(heading), 0.F, speed * std::sin(heading)};
    for (std::uint64_t tick = 0; tick < run_ticks; ++tick) {
        if (!active->tick(*world, *doll, pose, dt)) {
            return trace;
        }
        if (tick >= shove_tick && tick < shove_tick + shove_ticks) {
            for (const auto body : doll->bodies()) {
                const auto state = world->state(body);
                world->set_velocity(body, state->linear_velocity + push, state->angular_velocity);
            }
        }
        world->step(dt);
        const auto s = active->state();
        trace.stumbled |= s == ragdoll::ActiveState::Stumbling;
        trace.fell |= s == ragdoll::ActiveState::Fallen;
        trace.rose |= s == ragdoll::ActiveState::Rising;
        if (s != ragdoll::ActiveState::Animated && !trace.left) {
            trace.left = tick;
        }
        if (s == ragdoll::ActiveState::Animated && trace.left && !trace.returned) {
            trace.returned = tick;
        }
        trace.worst_angle = std::max(trace.worst_angle, active->angle());
        trace.worst_offset = std::max(trace.worst_offset, active->offset());
    }
    trace.final_angle = active->angle();
    trace.final_offset = active->offset();
    trace.final_state = active->state();
    trace.hash = world->state_hash();
    trace.made = true;
    return trace;
}

// The fixture's two claims, as the test reads them.
bool left_pose(const Trace& t) { return t.left.has_value(); }
bool came_back(const Trace& t, const ragdoll::ActiveProfile& p)
{
    return t.returned.has_value() && t.final_state == ragdoll::ActiveState::Animated && t.final_angle <= p.recovered_angle
        && t.final_offset <= p.recovered_offset;
}

void print(const char* name, const Trace& t)
{
    std::cout << name << ": left " << (t.left ? static_cast<long long>(*t.left) : -1LL) << " returned "
              << (t.returned ? static_cast<long long>(*t.returned) : -1LL) << " worst " << t.worst_angle << " rad "
              << t.worst_offset << " m, final " << t.final_angle << " rad " << t.final_offset << " m"
              << (t.stumbled ? " stumbled" : "") << (t.fell ? " fell" : "") << (t.rose ? " rose" : "") << '\n';
}

void test_refusals()
{
    std::string error;
    check(!ragdoll::ActiveRagdoll::create(std::nullopt, error) && !error.empty(),
          "no profile: the feature is off and create refuses");
    const auto refused = [&](auto change, const char* why) {
        auto p = test_profile();
        change(p);
        error.clear();
        check(!ragdoll::ActiveRagdoll::create(p, error) && !error.empty(), why);
    };
    refused([](auto& p) { p.strength = 0.F; }, "strength 0 is refused");
    refused([](auto& p) { p.strength = 1.5F; }, "strength above 1 is refused");
    refused([](auto& p) { p.lie_seconds = std::numeric_limits<float>::quiet_NaN(); }, "a NaN time is refused");
    refused([](auto& p) { p.rise_seconds = -1.F; }, "a negative time is refused");
    refused([](auto& p) { p.stumble_angle = p.fall_angle; }, "stumble angle not below fall angle is refused");
    refused([](auto& p) { p.recovered_angle = p.stumble_angle; }, "recovered angle not below stumble angle is refused");
    refused([](auto& p) { p.recovered_offset = p.fall_offset; }, "recovered offset not below fall offset is refused");
    check(ragdoll::ActiveRagdoll::create(test_profile(), error).has_value(), "the test profile is accepted");
}

// Ragdoll::create ignores Desc::active: the world is the same with or without it.
void test_off_keeps_world()
{
    std::string error;
    const auto asset = training_mannequin::make();
    const auto pose = animation::rest_pose(asset.skeleton);
    std::uint64_t hashes[2] = {};
    for (int with = 0; with < 2; ++with) {
        auto world = physics::World::create({}, error);
        auto desc = ragdoll_blend::mannequin_bones();
        if (with != 0) {
            desc.active = test_profile();
        }
        auto doll = ragdoll::Ragdoll::create(*world, asset.skeleton, pose, {{0.0, .1, 0.0}, {}, 1.F}, desc, error);
        check(doll.has_value(), "ragdoll made");
        for (int i = 0; doll && i < 30; ++i) {
            doll->follow(*world, pose, 0.F, dt);
            world->step(dt);
        }
        hashes[with] = world->state_hash();
    }
    check(hashes[0] == hashes[1], "an unused active profile leaves the world's hash unchanged");
}

void test_tick_refusals()
{
    std::string error;
    auto world = physics::World::create({}, error);
    const auto asset = training_mannequin::make();
    const auto pose = animation::rest_pose(asset.skeleton);
    auto doll = ragdoll::Ragdoll::create(*world, asset.skeleton, pose, {{0.0, .1, 0.0}, {}, 1.F},
                                         ragdoll_blend::mannequin_bones(), error);
    auto active = ragdoll::ActiveRagdoll::create(test_profile(), error);
    const auto before = world->state_hash();
    check(!active->tick(*world, *doll, pose, 0.F), "a zero step is refused");
    check(!active->tick(*world, *doll, pose, std::numeric_limits<float>::infinity()), "an infinite step is refused");
    check(!active->tick(*world, *doll, animation::Pose{}, dt), "a pose of the wrong size is refused");
    check(world->state_hash() == before, "refused ticks leave the world unchanged");
    doll->destroy(*world);
    check(!active->tick(*world, *doll, pose, dt), "a destroyed ragdoll is refused");
}

void test_fixture()
{
    const auto profile = test_profile();

    const Trace still = run(0.F, profile);
    print("still", still);
    check(still.made, "still run made");
    check(!left_pose(still), "with no shove the mannequin stays Animated");
    check(still.worst_angle <= profile.stumble_angle && still.worst_offset <= profile.recovered_offset,
          "with no shove the motors hold the standing pose");

    const Trace stumble = run(2.F, profile);
    print("stumble", stumble);
    check(stumble.made, "stumble run made");
    check(left_pose(stumble) && stumble.stumbled && !stumble.fell, "a 2 m/s shove is a stumble, not a fall");
    check(stumble.worst_angle > profile.stumble_angle, "the stumble leaves the animated pose");
    check(came_back(stumble, profile), "the stumble recovers to the animated pose");
    check(stumble.returned && *stumble.returned < shove_tick + 3 * hz, "the stumble recovers within 3 s");

    const Trace fall = run(8.F, profile);
    print("fall", fall);
    check(fall.made, "fall run made");
    check(fall.fell && fall.rose, "an 8 m/s shove is a fall, then a rise");
    check(came_back(fall, profile), "the fall recovers to the animated pose");

    const Trace again = run(8.F, profile);
    check(again.hash == fall.hash && again.returned == fall.returned, "the fixed-seed fixture repeats exactly");

    // Deliberate breaks: the checks above must catch a body that never leaves and one that never returns.
    check(!left_pose(still), "break 1: a run with no shove is caught as never leaving");
    auto weak = profile;
    weak.strength = .001F;
    const Trace stuck = run(2.F, weak);
    print("weak motors", stuck);
    check(stuck.made && left_pose(stuck) && !came_back(stuck, weak), "break 2: motors too weak are caught as never returning");
}

} // namespace

int main()
{
    test_refusals();
    test_off_keeps_world();
    test_tick_refusals();
    test_fixture();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "physics.ragdoll_active: ok\n";
    return 0;
}
