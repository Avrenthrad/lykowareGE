// player.directions (ADR 0074): directional fluidity on real physics, through the shared
// direction-change fixture (samples/course/direction_fixture.hpp). Continuous heading,
// mid-stride redirects that keep their speed, pivots only where the data says, the camera
// followed closely, strafing and backpedalling that never pop, and the same result at any
// frame rate.

#include "direction_fixture.hpp"

#include <lykoware/clock.hpp>
#include <lykoware/locomotion.hpp>
#include <lykoware/physics.hpp>
#include <lykoware/player.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

namespace {
namespace physics = lykoware::physics;
namespace loco = lykoware::locomotion;
namespace math = lykoware::math;
namespace fixture = direction_fixture;

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
constexpr float degree = pi / 180.F;

float wrap(float angle)
{
    return std::remainder(angle, 2.F * pi);
}

struct Sample {
    std::size_t segment = 0;
    int into = 0;
    loco::State motor;
    float camera = 0;   // the camera's yaw
    math::Vec3 moved{}; // the body's real horizontal velocity over the step
};
struct Run {
    std::vector<Sample> samples;
    std::uint64_t hash = 0;
    loco::Tuning tuning;
    loco::Profile profile;
};

// Plays the whole fixture: steering before every tick with that tick's intent, and drawing at
// `frames_per_second` (frames decide only when steps happen and what is drawn).
Run play(float agility, double frames_per_second = 60)
{
    std::string error;
    auto world = physics::World::create({}, error);
    physics::BodyDesc floor;
    floor.shape = physics::BoxShape{{80.F, .5F, 80.F}};
    floor.motion = physics::MotionType::Static;
    floor.position = {0, -.5, 0};
    if (!world || !world->create_body(floor, error)) {
        std::cerr << "FATAL: world: " << error << '\n';
        std::exit(EXIT_FAILURE);
    }
    lykoware::PlayerConfig config;
    config.body.position = {0.0, 0.1, 0.0};
    Run run;
    run.profile.base.agility = agility;
    config.locomotion = run.profile;
    auto player = lykoware::PlayerCharacter::create(*world, config, error);
    auto clock = lykoware::FixedStep::create({}, error);
    if (!player || !clock) {
        std::cerr << "FATAL: player: " << error << '\n';
        std::exit(EXIT_FAILURE);
    }
    run.tuning = *loco::resolve(run.profile);
    const auto step_seconds = static_cast<float>(clock->tick_seconds());
    int ticks = 0;
    while (ticks < fixture::total_ticks()) {
        const auto frame = clock->advance(static_cast<std::int64_t>(1e9 / frames_per_second));
        for (std::uint32_t i = 0; i < frame.ticks && ticks < fixture::total_ticks(); ++i) {
            player->steer(fixture::intent(ticks, step_seconds));
            const math::DVec3 before = player->state().position;
            player->before_step(step_seconds);
            (void)world->step(step_seconds);
            const math::DVec3 after = player->state().position;
            Sample sample;
            const auto at = fixture::position(ticks);
            sample.segment = at.segment;
            sample.into = at.into;
            sample.motor = *player->locomotion();
            sample.camera = player->camera().yaw();
            sample.moved = {static_cast<float>((after.x - before.x) / static_cast<double>(step_seconds)), 0.F,
                            static_cast<float>((after.z - before.z) / static_cast<double>(step_seconds))};
            run.samples.push_back(sample);
            ++ticks;
        }
        (void)player->view(static_cast<float>(1.0 / frames_per_second), static_cast<float>(frame.alpha));
    }
    run.hash = world->state_hash();
    return run;
}

std::size_t segment(std::string_view name)
{
    for (std::size_t i = 0; i < fixture::segments.size(); ++i) {
        if (name == fixture::segments[i].name) {
            return i;
        }
    }
    std::cerr << "FATAL: no segment " << name << '\n';
    std::exit(EXIT_FAILURE);
}
// The samples of one segment from `from_seconds` into it.
std::vector<const Sample*> in(const Run& run, std::string_view name, float from_seconds = 0)
{
    std::vector<const Sample*> out;
    const std::size_t wanted = segment(name);
    for (const Sample& sample : run.samples) {
        if (sample.segment == wanted && static_cast<float>(sample.into) * tick >= from_seconds) {
            out.push_back(&sample);
        }
    }
    return out;
}
std::uint64_t pivots_in(const Run& run, std::string_view name)
{
    const auto samples = in(run, name);
    const std::size_t first = static_cast<std::size_t>(samples.front() - run.samples.data());
    const std::uint64_t before = first == 0 ? 0 : run.samples[first - 1].motor.pivots;
    return samples.back()->motor.pivots - before;
}
// The world yaw of a push at `degrees` relative to a camera at yaw `camera`.
float push_yaw(float camera, float degrees)
{
    return wrap(camera + degrees * degree);
}

void test_no_pops(const Run& run, const std::string& label)
{
    // Every step: the body's velocity changes no more than the motor's limits allow; heading,
    // facing and travel angle move no faster than their rates. A pop would break one of these.
    const auto& t = run.tuning;
    const float sprint = run.profile.gait_speeds[3];
    float worst_velocity = 0, worst_heading = 0, worst_facing = 0, worst_travel = 0;
    for (std::size_t i = 1; i < run.samples.size(); ++i) {
        const Sample &a = run.samples[i - 1], &b = run.samples[i];
        const float rate =
            (t.turn_rate_rest + (t.turn_rate_sprint - t.turn_rate_rest) * std::min(a.motor.speed / sprint, 1.F)) * degree * tick;
        const float facing_rate = t.facing_rate * degree * tick;
        const float allowed = std::max({t.acceleration, t.braking, t.pivot_braking}) * tick + a.motor.speed * rate;
        worst_velocity = std::max(worst_velocity, std::hypot(b.moved.x - a.moved.x, b.moved.z - a.moved.z) - allowed);
        if (a.motor.speed > .01F && b.motor.speed > .01F) {
            worst_heading = std::max(worst_heading, std::abs(wrap(b.motor.heading - a.motor.heading)) - rate);
            worst_travel = std::max(worst_travel, std::abs(wrap(b.motor.travel - a.motor.travel)) - rate - facing_rate);
        }
        worst_facing = std::max(worst_facing, std::abs(wrap(b.motor.facing - a.motor.facing)) - facing_rate);
    }
    check(worst_velocity < .02F, label + ": the body's velocity never jumps: " + std::to_string(worst_velocity) + " m/s over");
    check(worst_heading < 1e-4F && worst_facing < 1e-4F && worst_travel < 1e-3F,
          label + ": heading, facing and travel never jump: " + std::to_string(worst_heading) + ", " +
              std::to_string(worst_facing) + ", " + std::to_string(worst_travel));
}

void test_fixture(const Run& run)
{
    test_no_pops(run, "agility 0.5");
    const float run_speed = run.profile.gait_speeds[2];

    // Mid-stride redirects inside the pivot angle (112.5 degrees here) keep their speed and
    // never pivot; wider ones pivot exactly once.
    for (const char* name : {"cut 45", "cut 90"}) {
        float slowest = 1e9F;
        for (const Sample* s : in(run, name)) {
            slowest = std::min(slowest, s->motor.speed);
        }
        check(pivots_in(run, name) == 0 && std::abs(slowest - run_speed) < 1e-4F,
              std::string{name} + ": steered round at full speed: " + std::to_string(slowest) + " m/s at slowest");
    }
    for (const char* name : {"cut 135", "cut 180"}) {
        check(pivots_in(run, name) == 1, std::string{name} + ": one plant and pivot");
        const Sample* end = in(run, name).back();
        check(std::abs(end->motor.speed - run_speed) < 1e-4F && end->motor.legs == loco::Legs::Moving,
              std::string{name} + ": and back to full speed the new way");
    }
    // Each cut ends heading exactly where the stick points.
    for (const char* name : {"cut 45", "cut 90", "cut 135", "cut 180"}) {
        const Sample* end = in(run, name).back();
        const float wanted = push_yaw(end->camera, fixture::segments[segment(name)].push_degrees);
        check(std::abs(wrap(end->motor.heading - wanted)) < 1e-3F, std::string{name} + ": ends heading where the stick points");
    }

    // Loops: the stick sweeps round continuously. The heading follows it closely and takes
    // every direction, not eight.
    std::vector<float> directions; // degrees
    float worst_lag = 0, slowest = 1e9F;
    for (const char* name : {"loop left", "loop right"}) {
        for (const Sample* s : in(run, name, .5F)) {
            const int tick_index = static_cast<int>(s - run.samples.data());
            worst_lag = std::max(worst_lag, std::abs(wrap(s->motor.heading - push_yaw(s->camera, fixture::push_degrees(tick_index)))));
            slowest = std::min(slowest, s->motor.speed);
            directions.push_back(wrap(s->motor.heading) / degree);
        }
        check(pivots_in(run, name) == 0, std::string{name} + ": a sweeping stick never pivots");
    }
    // Every heading round the circle is taken: the widest gap between headings the body actually
    // travelled is a few degrees: the stick sweeps 2 degrees a tick (eight-way movement would leave 45).
    std::sort(directions.begin(), directions.end());
    float widest = directions.front() + 360.F - directions.back();
    for (std::size_t i = 1; i < directions.size(); ++i) {
        widest = std::max(widest, directions[i] - directions[i - 1]);
    }
    check(widest < 5.F, "the loops take every heading, not eight: widest gap " + std::to_string(widest) + " degrees");
    check(worst_lag < 5.F * degree, "the heading follows a sweeping stick within 5 degrees: " + std::to_string(worst_lag / degree));
    check(std::abs(slowest - run_speed) < 1e-4F, "and keeps full speed round the loops: " + std::to_string(slowest));

    // The camera turns while the stick holds forward: the body follows the camera closely,
    // without fighting it.
    worst_lag = 0;
    for (const Sample* s : in(run, "camera sweep", .5F)) {
        worst_lag = std::max(worst_lag, std::abs(wrap(s->motor.heading - s->camera)));
    }
    check(worst_lag < 2.F * degree, "with the camera turning, the heading follows within 2 degrees: " + std::to_string(worst_lag / degree));
    check(pivots_in(run, "camera sweep") == 0, "and never pivots");

    // Strafe box: aiming, the body faces the camera throughout and travels forward, right,
    // back and left of it, slower sideways and backward by the data.
    float worst_facing = 0;
    for (const char* name : {"strafe forward", "strafe right", "strafe back", "strafe left", "backpedal diagonal"}) {
        for (const Sample* s : in(run, name, .5F)) {
            worst_facing = std::max(worst_facing, std::abs(wrap(s->motor.facing - s->camera)));
            check(s->motor.facing_held, std::string{name} + ": the facing is held");
        }
    }
    check(worst_facing < .5F * degree, "aiming, the body faces where the camera looks: " + std::to_string(worst_facing / degree) + " degrees");
    struct Side {
        const char* name;
        float travel; // degrees
        float scale;
    };
    const float diagonal = run.profile.strafe_scale + (run.profile.backward_scale - run.profile.strafe_scale) * std::sqrt(.5F);
    for (const Side side : {Side{"strafe forward", 0.F, 1.F}, Side{"strafe right", -90.F, run.profile.strafe_scale},
                            Side{"strafe back", 180.F, run.profile.backward_scale}, Side{"strafe left", 90.F, run.profile.strafe_scale},
                            Side{"backpedal diagonal", 135.F, diagonal}}) {
        const Sample* end = in(run, side.name).back();
        check(std::abs(wrap(end->motor.travel - side.travel * degree)) < 1.F * degree,
              std::string{side.name} + ": travels " + std::to_string(side.travel) + " degrees from facing: " +
                  std::to_string(end->motor.travel / degree));
        check(std::abs(end->motor.speed - run_speed * side.scale) < .01F && std::abs(std::hypot(end->moved.x, end->moved.z) - end->motor.speed) < .02F,
              std::string{side.name} + ": at " + std::to_string(side.scale) + " of the run speed: " + std::to_string(end->motor.speed));
    }
    // Releasing the aim, the body turns back to its travel and stops.
    const Sample* released = in(run, "release").back();
    check(!released->motor.facing_held && released->motor.speed == 0.F && released->motor.gait == loco::Gait::Idle,
          "released, it stops and stands");
}

void test_agility_on_fixture()
{
    // The same fixture: a heavy character sweeps round the 90 and 135 degree cuts, a sharp one
    // plants and pivots on them.
    const Run heavy = play(0.F), sharp = play(1.F);
    test_no_pops(heavy, "agility 0");
    test_no_pops(sharp, "agility 1");
    const auto cuts = [](const Run& run) {
        return std::array<std::uint64_t, 4>{pivots_in(run, "cut 45"), pivots_in(run, "cut 90"), pivots_in(run, "cut 135"),
                                            pivots_in(run, "cut 180")};
    };
    const auto h = cuts(heavy), s = cuts(sharp);
    check(h == std::array<std::uint64_t, 4>{0, 0, 0, 1}, "heavy pivots only to reverse");
    check(s == std::array<std::uint64_t, 4>{0, 1, 1, 1}, "sharp pivots on every cut past 75 degrees");
    // Sharp follows a sweeping stick more tightly.
    const auto lag = [](const Run& run) {
        float worst = 0;
        for (const char* name : {"loop left", "loop right"}) {
            for (const Sample* sample : in(run, name, .5F)) {
                const auto index = static_cast<int>(sample - run.samples.data());
                worst = std::max(worst, std::abs(wrap(sample->motor.heading - push_yaw(sample->camera, fixture::push_degrees(index)))));
            }
        }
        return worst;
    };
    check(lag(sharp) <= lag(heavy), "sharp follows the loops at least as tightly: " + std::to_string(lag(sharp) / degree) +
                                        " vs " + std::to_string(lag(heavy) / degree) + " degrees");
}

void test_frame_rates(const Run& base)
{
    for (const double rate : {30.0, 144.0, 241.0}) {
        const Run other = play(.5F, rate);
        bool same = other.hash == base.hash && other.samples.size() == base.samples.size();
        for (std::size_t i = 0; same && i < base.samples.size(); ++i) {
            same = other.samples[i].motor.heading == base.samples[i].motor.heading &&
                   other.samples[i].motor.facing == base.samples[i].motor.facing &&
                   other.samples[i].motor.speed == base.samples[i].motor.speed;
        }
        check(same, "the fixture plays identically at " + std::to_string(rate) + " frames a second");
    }
}
} // namespace

int main()
{
    const Run base = play(.5F);
    test_fixture(base);
    test_agility_on_fixture();
    test_frame_rates(base);
    if (failures != 0) {
        std::cerr << failures << " player direction assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "player directions passed (" << base.samples.size() << " ticks, " << base.samples.back().motor.pivots << " pivots)\n";
    return EXIT_SUCCESS;
}
