// player.locomotion (ADR 0074): the player moved by the locomotion motor, on real physics.
// Feel checks that a regression would break: no sliding or snapping, the same result at any
// frame rate, and sharp and heavy characters differing as their data says.

#include <lykoware/clock.hpp>
#include <lykoware/input.hpp>
#include <lykoware/locomotion.hpp>
#include <lykoware/physics.hpp>
#include <lykoware/player.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

namespace {
namespace physics = lykoware::physics;
namespace loco = lykoware::locomotion;
namespace math = lykoware::math;
using lykoware::PlayerCharacter;
using lykoware::PlayerConfig;
using lykoware::PlayerIntent;

int failures = 0;
void check(bool ok, const std::string& why)
{
    if (!ok) {
        std::cerr << "FAIL: " << why << '\n';
        ++failures;
    }
}

constexpr float tick = 1.F / 60.F;
constexpr float pi = std::numbers::pi_v<float>;

struct Scene {
    std::unique_ptr<physics::World> world;
    std::unique_ptr<PlayerCharacter> player;
    // How the feet really moved over the latest step, measured from their positions: the world
    // reports the velocity it was asked for, which is not evidence.
    math::Vec3 moved{};
};
// A flat floor, an optional wall across the way at z = -6, and a player with a profile of the
// given agility (or none).
Scene make(std::optional<float> agility, bool wall = false)
{
    std::string error;
    Scene scene;
    scene.world = physics::World::create({}, error);
    if (!scene.world) {
        std::cerr << "FATAL: world: " << error << '\n';
        std::exit(EXIT_FAILURE);
    }
    const auto block = [&](math::Vec3 half, math::DVec3 at) {
        physics::BodyDesc desc;
        desc.shape = physics::BoxShape{half};
        desc.motion = physics::MotionType::Static;
        desc.position = at;
        if (!scene.world->create_body(desc, error)) {
            std::cerr << "FATAL: body: " << error << '\n';
            std::exit(EXIT_FAILURE);
        }
    };
    block({60.F, .5F, 60.F}, {0, -.5, 0});
    if (wall) {
        block({10.F, 2.F, .5F}, {0, 2, -6.5});
    }
    PlayerConfig config;
    config.body.position = {0.0, 0.1, 0.0};
    if (agility) {
        loco::Profile profile;
        profile.base.agility = *agility;
        config.locomotion = profile;
    }
    scene.player = PlayerCharacter::create(*scene.world, config, error);
    if (!scene.player) {
        std::cerr << "FATAL: player: " << error << '\n';
        std::exit(EXIT_FAILURE);
    }
    for (int i = 0; i < 30; ++i) {
        scene.player->before_step(tick);
        (void)scene.world->step(tick);
    }
    return scene;
}
PlayerIntent push(float forward, float right = 0, bool run = false, bool walk = false)
{
    PlayerIntent intent;
    intent.forward = forward;
    intent.right = right;
    intent.run = run;
    intent.walk = walk;
    return intent;
}
float horizontal_speed(const Scene& scene)
{
    return std::hypot(scene.moved.x, scene.moved.z);
}
void step(Scene& scene, const PlayerIntent& intent, int ticks = 1)
{
    scene.player->steer(intent);
    for (int i = 0; i < ticks; ++i) {
        const math::DVec3 before = scene.player->state().position;
        scene.player->before_step(tick);
        (void)scene.world->step(tick);
        const math::DVec3 after = scene.player->state().position;
        scene.moved = {static_cast<float>((after.x - before.x) / static_cast<double>(tick)), 0.F,
                       static_cast<float>((after.z - before.z) / static_cast<double>(tick))};
    }
}

void test_no_instant_speed()
{
    // Without a profile the character is at walking speed on the first step, as before.
    Scene legacy = make(std::nullopt);
    check(legacy.player->locomotion() == nullptr, "without a profile there is no motor");
    step(legacy, push(1));
    check(std::abs(horizontal_speed(legacy) - 4.F) < .05F, "without a profile movement starts at once, as before: " +
                                                                    std::to_string(horizontal_speed(legacy)));

    // With one, the body gains speed at the acceleration, and physics follows the motor.
    Scene scene = make(.5F);
    const auto* motor = scene.player->locomotion();
    check(motor != nullptr, "with a profile there is a motor");
    if (motor == nullptr) {
        return;
    }
    const float acceleration = (6.F + 24.F) * .5F;
    float previous = 0, worst_step = 0, worst_follow = 0;
    int steps = 0;
    while (motor->speed < 5.F && steps < 600) {
        step(scene, push(1));
        ++steps;
        const float actual = horizontal_speed(scene);
        worst_step = std::max(worst_step, actual - previous);
        worst_follow = std::max(worst_follow, std::abs(actual - motor->speed));
        previous = actual;
    }
    check(worst_step <= acceleration * tick + .01F, "the body never gains more than one step of acceleration: " +
                                                        std::to_string(worst_step) + " m/s in a step");
    check(worst_follow < .02F, "physics follows the motor's speed on flat ground: " + std::to_string(worst_follow));
    check(std::abs(static_cast<float>(steps) - 5.F / (acceleration * tick)) <= 2.F,
          "it reaches a run's speed in the time its acceleration gives: " + std::to_string(steps) + " steps");
    step(scene, push(1), 30);
    check(motor->gait == loco::Gait::Run, "and settles into the run gait");

    // Letting go: it comes to rest over v^2 / 2b of real ground, not at once.
    const double start = scene.player->state().position.z;
    const float braking = (9.F + 34.F) * .5F;
    int stopping = 0;
    while (horizontal_speed(scene) > 1e-3F && stopping < 600) {
        step(scene, push(0));
        ++stopping;
    }
    const double travelled = std::abs(scene.player->state().position.z - start);
    const double expected = 25.0 / (2.0 * braking);
    check(stopping > 5 && std::abs(travelled - expected) < .15, "it stops over its braking distance on the ground: " +
                                                                   std::to_string(travelled) + " vs " + std::to_string(expected) + " m");
    check(motor->gait == loco::Gait::Idle && motor->legs == loco::Legs::Stopped, "and ends idle and still");
}

void test_speeds_from_input()
{
    Scene scene = make(.5F);
    const auto settled = [&](const PlayerIntent& intent) {
        step(scene, intent, 240);
        return horizontal_speed(scene);
    };
    check(std::abs(settled(push(1)) - 5.F) < .02F && scene.player->locomotion()->gait == loco::Gait::Run, "a full push is a run");
    check(std::abs(settled(push(1, 0, true)) - 7.F) < .02F && scene.player->locomotion()->gait == loco::Gait::Sprint,
          "with run held it is a sprint");
    check(std::abs(settled(push(1, 0, false, true)) - 1.6F) < .02F && scene.player->locomotion()->gait == loco::Gait::Walk,
          "with walk held it is a walk");
    check(std::abs(settled(push(.5F)) - 2.5F) < .02F, "half a stick is half the speed");
    check(std::abs(settled(push(.64F)) - 3.2F) < .02F && scene.player->locomotion()->gait == loco::Gait::Jog,
          "and part of a stick reaches the gait in between");
    // Diagonals are no faster.
    check(std::abs(settled(push(1, 1)) - 5.F) < .02F, "a diagonal push is no faster");
    // Variants and agility reach the motor.
    check(!scene.player->select_locomotion("combat") && scene.player->select_locomotion("") && scene.player->set_agility(1.F) &&
              !scene.player->set_agility(std::nanf("")),
          "a variant is selected by name and agility set through the player");
    Scene legacy = make(std::nullopt);
    check(!legacy.player->select_locomotion("") && !legacy.player->set_agility(.5F), "without a profile there is nothing to select");
}

// Largest change of the body's real horizontal velocity in one step, over a fixed-seed storm of
// pushes, against what the motor's limits allow.
void test_no_slide()
{
    for (const float agility : {0.F, .5F, 1.F}) {
        Scene scene = make(agility);
        loco::Profile profile;
        profile.base.agility = agility;
        const auto tuning = *loco::resolve(profile);
        std::uint32_t seed = 4242;
        const auto random = [&] {
            seed = seed * 1664525U + 1013904223U;
            return static_cast<float>(seed >> 8U) / 16777216.F;
        };
        PlayerIntent intent;
        float worst = 0;
        math::Vec3 previous = scene.moved;
        for (int i = 0; i < 6000; ++i) {
            if (i % 31 == 0) {
                const float angle = random() * 2.F * pi, size = random() < .2F ? 0.F : random();
                intent = push(std::cos(angle) * size, std::sin(angle) * size, random() < .3F, random() < .2F);
                intent.yaw = (random() - .5F) * .6F; // the camera swings too
            } else {
                intent.yaw = 0;
            }
            const float speed_before = horizontal_speed(scene);
            step(scene, intent);
            const math::Vec3 now = scene.moved;
            const float change = std::hypot(now.x - previous.x, now.z - previous.z);
            const float turn_rate = std::max(tuning.turn_rate_rest, tuning.turn_rate_sprint) * pi / 180.F;
            const float allowed = std::max({tuning.acceleration, tuning.braking, tuning.pivot_braking}) * tick +
                                  speed_before * turn_rate * tick;
            worst = std::max(worst, change - allowed);
            previous = now;
        }
        check(worst < .02F, "the body's velocity never jumps past its limits (agility " + std::to_string(agility) + "): " +
                                std::to_string(worst) + " m/s over");
    }
}

// The same pushes, keyed to simulation time, played at a given frame rate. Frames only decide
// when steps happen; the result must not depend on them.
struct Outcome {
    math::DVec3 position;
    loco::State state;
    std::uint64_t hash = 0;
    int ticks = 0;
};
Outcome play(double frames_per_second, float agility)
{
    Scene scene = make(agility);
    std::string error;
    auto clock = lykoware::FixedStep::create({}, error);
    if (!clock) {
        std::cerr << "FATAL: clock: " << error << '\n';
        std::exit(EXIT_FAILURE);
    }
    const auto step_seconds = static_cast<float>(clock->tick_seconds());
    const int total = 600;
    int ticks = 0;
    const auto intent_at = [](int at) {
        const int phase = at / 45;
        switch (phase % 8) {
        case 0: return push(1);
        case 1: return push(1, 1, true);
        case 2: return push(-1);
        case 3: return push(0);
        case 4: return push(0, -1, false, true);
        case 5: return push(.4F, .6F);
        case 6: return push(-1, 0, true);
        default: return push(1, -1);
        }
    };
    while (ticks < total) {
        const auto frame = clock->advance(static_cast<std::int64_t>(1e9 / frames_per_second));
        for (std::uint32_t i = 0; i < frame.ticks && ticks < total; ++i) {
            // The push in force is decided by simulation time, as a replay would.
            scene.player->steer(intent_at(ticks));
            scene.player->before_step(step_seconds);
            (void)scene.world->step(step_seconds);
            ++ticks;
        }
        (void)scene.player->view(static_cast<float>(1.0 / frames_per_second), static_cast<float>(frame.alpha));
    }
    return {scene.player->state().position, *scene.player->locomotion(), scene.world->state_hash(), ticks};
}

void test_frame_rate_independence()
{
    const Outcome base = play(60, .5F);
    for (const double rate : {30.0, 48.0, 144.0, 237.0}) {
        const Outcome other = play(rate, .5F);
        check(other.ticks == base.ticks && other.hash == base.hash && other.position.x == base.position.x &&
                  other.position.z == base.position.z && std::memcmp(&other.state.velocity, &base.state.velocity, sizeof(math::Vec3)) == 0 &&
                  other.state.facing == base.state.facing && other.state.pivots == base.state.pivots &&
                  other.state.lean_side == base.state.lean_side,
              "the same pushes give the same result at " + std::to_string(rate) + " frames a second");
    }
    check(base.state.pivots >= 2, "the sequence includes pivots: " + std::to_string(base.state.pivots));
    check(play(60, .9F).hash != base.hash, "and agility changes the result");
}

void test_sharp_versus_heavy()
{
    // On real ground: from a run, stop; and from a run, reverse.
    const auto stop_distance = [](float agility) {
        Scene scene = make(agility);
        step(scene, push(1), 240);
        const double start = scene.player->state().position.z;
        step(scene, push(0), 240);
        return std::abs(scene.player->state().position.z - start);
    };
    const auto reversal = [](float agility, double& carried) {
        Scene scene = make(agility);
        step(scene, push(1), 240);
        const double start = scene.player->state().position.z;
        double furthest = start;
        int ticks = 0;
        while (scene.moved.z < 4.5F && ticks < 600) { // forward is -Z; +Z is back
            step(scene, push(-1));
            furthest = std::min(furthest, scene.player->state().position.z);
            ++ticks;
        }
        carried = start - furthest;
        return ticks;
    };
    const double heavy_stop = stop_distance(0), sharp_stop = stop_distance(1);
    check(sharp_stop * 3 < heavy_stop && sharp_stop < .5 && heavy_stop > 1.2,
          "on the ground sharp stops in a third of heavy's distance: " + std::to_string(sharp_stop) + " vs " +
              std::to_string(heavy_stop) + " m");
    double heavy_carried = 0, sharp_carried = 0;
    const int heavy_ticks = reversal(0, heavy_carried), sharp_ticks = reversal(1, sharp_carried);
    check(sharp_ticks * 2 < heavy_ticks, "sharp reverses in under half the time: " + std::to_string(sharp_ticks) + " vs " +
                                             std::to_string(heavy_ticks) + " ticks");
    check(sharp_carried < heavy_carried && sharp_carried > .05,
          "both are carried on before they turn, sharp less far: " + std::to_string(sharp_carried) + " vs " +
              std::to_string(heavy_carried) + " m");
}

void test_wall_and_air()
{
    // Running into a wall: the motor does not wind up speed against it, so backing away does
    // not have to brake a speed the body never had.
    Scene scene = make(.5F, true);
    step(scene, push(1), 300);
    const auto* motor = scene.player->locomotion();
    check(scene.player->state().position.z > -6.0 && motor->speed < 1.F && horizontal_speed(scene) < .2F,
          "against a wall the motor's speed stays near the body's: " + std::to_string(motor->speed));
    step(scene, push(-1), 20);
    check(scene.moved.z > 1.F, "and it backs away at once: " + std::to_string(scene.moved.z));

    // A jump at a run keeps its horizontal speed in the air and does not brake when let go.
    Scene jumper = make(.5F);
    step(jumper, push(1), 240);
    PlayerIntent jump = push(1);
    jump.jump = true;
    step(jumper, jump);
    step(jumper, push(0), 10);
    check(jumper.player->state().ground != physics::GroundState::OnGround && jumper.player->locomotion()->legs == loco::Legs::Airborne &&
              std::abs(horizontal_speed(jumper) - 5.F) < .1F,
          "in the air the run's speed is kept: " + std::to_string(horizontal_speed(jumper)));
}

void test_facing_and_refusals()
{
    // The body turns to face its heading at the motor's rate, and is drawn between steps.
    Scene scene = make(.5F);
    PlayerIntent turn = push(1);
    turn.yaw = -pi / 2.F; // the camera now looks along +X
    step(scene, turn);
    const float after_one = scene.player->facing();
    check(std::abs(after_one) > 1e-3F && std::abs(after_one) < pi / 4.F, "the body starts to turn, and not all at once: " +
                                                                            std::to_string(after_one));
    const float between = scene.player->drawn_facing(.5F);
    check(std::abs(between - after_one * .5F) < 1e-4F, "the facing is drawn between steps");
    step(scene, push(1), 120);
    check(std::abs(scene.player->facing() + pi / 2.F) < 1e-3F && scene.moved.x > 4.9F,
          "it ends facing and moving where the camera looks");

    // Aiming: the body faces where the camera looks and strafes to its right.
    Scene aimer = make(.5F);
    PlayerIntent aim = push(0, 1);
    aim.aim = true;
    step(aimer, aim, 120);
    const auto* motor = aimer.player->locomotion();
    check(std::abs(aimer.player->facing()) < 1e-4F && motor->facing_held && std::abs(motor->travel + pi / 2.F) < 1e-3F &&
              std::abs(horizontal_speed(aimer) - 5.F * .9F) < .02F && aimer.moved.x > 4.4F,
          "aiming, the body faces the camera's way and strafes right at the strafe speed");
    // Without a profile aiming changes nothing: it turns to face its travel as before.
    Scene legacy = make(std::nullopt);
    step(legacy, aim, 60);
    check(std::abs(legacy.player->facing() + pi / 2.F) < 1e-4F, "without a profile aiming is ignored");

    // The aim input: the right mouse button, or the left trigger past half.
    lykoware::InputState input;
    const lykoware::PlayerInputConfig input_config;
    check(!lykoware::read_player_intent(input, input_config, tick, true).aim, "no input does not aim");
    input.mouse.buttons[static_cast<std::size_t>(lykoware::MouseButton::Right)].down = true;
    check(lykoware::read_player_intent(input, input_config, tick, true).aim, "the right mouse button aims");
    input = {};
    input.gamepad.axes[static_cast<std::size_t>(lykoware::GamepadAxis::LeftTrigger)] = .6F;
    check(lykoware::read_player_intent(input, input_config, tick, true).aim, "the left trigger past half aims");
    input.gamepad.axes[static_cast<std::size_t>(lykoware::GamepadAxis::LeftTrigger)] = .4F;
    check(!lykoware::read_player_intent(input, input_config, tick, true).aim, "a light touch of the trigger does not");
    input = {};
    input.keys[static_cast<std::size_t>(lykoware::Key::LeftCtrl)].down = true;
    check(lykoware::read_player_intent(input, input_config, tick, true).walk, "Ctrl walks");

    // An unusable profile creates nothing.
    std::string error;
    auto world = physics::World::create({}, error);
    PlayerConfig config;
    loco::Profile bad;
    bad.gait_speeds[0] = 9.F; // walk faster than jog
    config.locomotion = bad;
    check(PlayerCharacter::create(*world, config, error) == nullptr && !error.empty() && world->character_count() == 0,
          "an unusable locomotion profile creates no player: " + error);
}
} // namespace

int main()
{
    test_no_instant_speed();
    test_speeds_from_input();
    test_no_slide();
    test_frame_rate_independence();
    test_sharp_versus_heavy();
    test_wall_and_air();
    test_facing_and_refusals();
    if (failures != 0) {
        std::cerr << failures << " player locomotion assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "player locomotion passed\n";
    return EXIT_SUCCESS;
}
