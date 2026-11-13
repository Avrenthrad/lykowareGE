// locomotion.motor (ADR 0074): the locomotion motor on its own. Speed changes, braking, turn
// rate by speed, plant-and-pivot, gaits, agility, lean, the air, data, and repeatability.

#include <lykoware/locomotion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <new>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {
thread_local bool tracking = false;
thread_local std::size_t allocations = 0;
} // namespace
void* operator new(std::size_t n)
{
    if (tracking) {
        ++allocations;
    }
    if (auto* p = std::malloc(n ? n : 1)) {
        return p;
    }
    throw std::bad_alloc();
}
void* operator new[](std::size_t n)
{
    return ::operator new(n);
}
void operator delete(void* p) noexcept
{
    std::free(p);
}
void operator delete[](void* p) noexcept
{
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept
{
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept
{
    std::free(p);
}

namespace {
namespace loco = lykoware::locomotion;
namespace math = lykoware::math;
using math::Vec3;

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
constexpr float degrees = pi / 180.F;

loco::Motor make(float agility, loco::Profile profile = {})
{
    profile.base.agility = agility;
    std::string error;
    auto motor = loco::Motor::create(profile, error);
    if (!motor) {
        std::cerr << "FATAL: motor: " << error << '\n';
        std::exit(EXIT_FAILURE);
    }
    return *motor;
}
// A horizontal direction from a yaw (FollowCamera's convention: 0 faces -Z, positive turns left).
Vec3 toward(float yaw_degrees, float speed)
{
    const float yaw = yaw_degrees * degrees;
    return {-std::sin(yaw) * speed, 0.F, -std::cos(yaw) * speed};
}
loco::Request want(Vec3 velocity, bool grounded = true)
{
    loco::Request request;
    request.velocity = velocity;
    request.grounded = grounded;
    return request;
}
float length(Vec3 v)
{
    return std::sqrt(v.x * v.x + v.z * v.z);
}
float wrap(float angle)
{
    return std::remainder(angle, 2.F * pi);
}
// Steps until `done` or `limit` steps; returns the steps taken.
template <class Done>
int until(loco::Motor& motor, const loco::Request& request, int limit, Done done)
{
    for (int i = 1; i <= limit; ++i) {
        motor.step(request, tick);
        if (done(motor.state())) {
            return i;
        }
    }
    return limit + 1;
}
void settle(loco::Motor& motor, const loco::Request& request, int steps = 600)
{
    for (int i = 0; i < steps; ++i) {
        motor.step(request, tick);
    }
}
bool same(const loco::State& a, const loco::State& b)
{
    return std::memcmp(&a.velocity, &b.velocity, sizeof(a.velocity)) == 0 && a.speed == b.speed && a.heading == b.heading &&
           a.facing == b.facing && a.gait == b.gait && a.legs == b.legs && a.lean_forward == b.lean_forward &&
           a.lean_side == b.lean_side && a.pivots == b.pivots;
}

void test_start_and_stop()
{
    auto motor = make(.5F);
    const auto tuning = motor.tuning();
    // From rest, toward 5 m/s: the speed rises by the acceleration each step and never passes 5.
    float previous = 0, worst = 0;
    int steps = 0;
    bool moving = true;
    while (motor.state().speed < 5.F && steps < 600) {
        const auto& state = motor.step(want(toward(0, 5)), tick);
        ++steps;
        if (state.speed < 5.F) {
            worst = std::max(worst, std::abs((state.speed - previous) - tuning.acceleration * tick));
        }
        moving = moving && state.legs == loco::Legs::Moving && state.speed <= 5.F;
        previous = state.speed;
    }
    const float expected_steps = 5.F / (tuning.acceleration * tick);
    check(worst < 1e-4F && moving && std::abs(static_cast<float>(steps) - expected_steps) <= 1.F && motor.state().speed == 5.F,
          "a start gains speed at the acceleration and stops at the wanted speed: " + std::to_string(steps) + " steps, " +
              std::to_string(worst));
    check(std::abs(motor.state().velocity.z + 5.F) < 1e-5F && std::abs(motor.state().velocity.x) < 1e-5F &&
              motor.state().velocity.y == 0.F,
          "the velocity points the way that was wanted");

    // Let go: the speed falls by the braking each step to exactly nothing, over v^2 / 2b.
    double travelled = 0;
    previous = 5.F;
    worst = 0;
    bool braking = true;
    steps = 0;
    while (motor.state().speed > 0.F && steps < 600) {
        const auto& state = motor.step(want({}), tick);
        ++steps;
        travelled += static_cast<double>(state.speed) * tick;
        if (state.speed > 0.F) {
            worst = std::max(worst, std::abs((previous - state.speed) - tuning.braking * tick));
            braking = braking && state.legs == loco::Legs::Braking;
        }
        previous = state.speed;
    }
    const double expected = 25.0 / (2.0 * tuning.braking);
    check(worst < 1e-4F && braking && motor.state().speed == 0.F && motor.state().legs == loco::Legs::Stopped,
          "a stop sheds speed at the braking rate and ends still: " + std::to_string(worst));
    check(std::abs(travelled - expected) < 5.0 * tick, "the stopping distance is v^2 / 2b: " + std::to_string(travelled) + " vs " +
                                                           std::to_string(expected));
    // A standing start goes any way at once: no turning from rest.
    const auto& first = motor.step(want(toward(137, 3)), tick);
    check(std::abs(wrap(first.heading - 137.F * degrees)) < 1e-4F && std::abs(first.speed - tuning.acceleration * tick) < 1e-5F,
          "a standing start sets off the wanted way at once");
}

void test_analog_and_cap()
{
    auto motor = make(.5F);
    settle(motor, want(toward(40, 2.4F)));
    check(motor.state().speed == 2.4F, "an analog speed is kept exactly: " + std::to_string(motor.state().speed));
    settle(motor, want(toward(40, 30)));
    check(motor.state().speed == 7.F, "the wanted speed is limited to the sprint speed: " + std::to_string(motor.state().speed));
    // Slowing to a lower wanted speed brakes down to it, not through it.
    float lowest = 7.F;
    bool braked = false;
    for (int i = 0; i < 200; ++i) {
        const auto& state = motor.step(want(toward(40, 3)), tick);
        lowest = std::min(lowest, state.speed);
        braked = braked || state.legs == loco::Legs::Braking;
    }
    check(lowest == 3.F && braked && motor.state().speed == 3.F, "slowing brakes down to the lower speed and holds it");
}

void test_turn_rate_by_speed()
{
    auto motor = make(.5F);
    const auto tuning = motor.tuning();
    const auto rate_at = [&](float speed) {
        return (tuning.turn_rate_rest + (tuning.turn_rate_sprint - tuning.turn_rate_rest) * (speed / 7.F)) * degrees;
    };
    for (const float speed : {1.6F, 3.2F, 7.F}) {
        motor.reset();
        settle(motor, want(toward(0, speed)));
        // 60 degrees to the left is inside the pivot angle: the heading steers, the speed stays.
        float previous = motor.state().heading, fastest = 0;
        bool held = true;
        const int steps = until(motor, want(toward(60, speed)), 600, [&](const loco::State& state) {
            fastest = std::max(fastest, std::abs(wrap(state.heading - previous)));
            held = held && state.speed == speed && state.legs == loco::Legs::Moving;
            previous = state.heading;
            return std::abs(wrap(state.heading - 60.F * degrees)) < 1e-4F;
        });
        const float expected = 60.F * degrees / (rate_at(speed) * tick);
        check(std::abs(fastest - rate_at(speed) * tick) < 1e-4F && held && std::abs(static_cast<float>(steps) - expected) <= 1.5F &&
                  motor.state().pivots == 0,
              "at " + std::to_string(speed) + " m/s the heading steers at its rate without losing speed: " +
                  std::to_string(steps) + " steps vs " + std::to_string(expected));
    }
    check(rate_at(7.F) < rate_at(1.6F), "the turn rate falls as speed rises");
    // Turning the other way steers the other way.
    motor.reset();
    settle(motor, want(toward(0, 3.2F)));
    motor.step(want(toward(-60, 3.2F)), tick);
    check(wrap(motor.state().heading) < 0.F, "a turn to the right steers right");
}

// Runs at `speed`, then asks for `turn` degrees off. Records what the redirect did.
struct Redirect {
    int steps_to_new_heading = 0; // until travelling the new way at 90% of the speed
    float lowest_speed = 0;
    float heading_moved_while_fast = 0; // radians the heading turned while above the pivot speed
    double overshoot = 0;               // metres travelled along the old heading after the request
    std::uint64_t pivots = 0;
    bool pivot_legs = false;
    float worst_brake_error = 0; // m/s: how far a braking step of the pivot is from the pivot braking
};
Redirect redirect(float agility, float speed, float turn)
{
    auto motor = make(agility);
    settle(motor, want(toward(0, speed)));
    const float pivot_speed = motor.tuning().pivot_speed;
    Redirect result;
    result.lowest_speed = speed;
    const float start = motor.state().heading;
    const Vec3 target = toward(turn, 1);
    bool done = false;
    for (int i = 1; i <= 1200; ++i) {
        const float speed_before = motor.state().speed;
        const auto& state = motor.step(want(toward(turn, speed)), tick);
        if (state.legs == loco::Legs::Pivoting && state.speed > pivot_speed + 1e-4F) {
            result.worst_brake_error =
                std::max(result.worst_brake_error, std::abs((speed_before - state.speed) - motor.tuning().pivot_braking * tick));
        }
        result.lowest_speed = std::min(result.lowest_speed, state.speed);
        result.overshoot += static_cast<double>(-state.velocity.z) * tick > 0 ? static_cast<double>(-state.velocity.z) * tick : 0;
        if (state.speed > pivot_speed + 1e-4F && state.legs == loco::Legs::Pivoting) {
            result.heading_moved_while_fast = std::max(result.heading_moved_while_fast, std::abs(wrap(state.heading - start)));
        }
        result.pivot_legs = result.pivot_legs || state.legs == loco::Legs::Pivoting;
        if (!done && state.velocity.x * target.x + state.velocity.z * target.z >= .9F * speed) {
            result.steps_to_new_heading = i;
            done = true;
        }
    }
    result.pivots = motor.state().pivots;
    check(done && motor.state().speed == speed, "a redirect ends at speed the new way");
    return result;
}

void test_pivot()
{
    auto motor = make(.5F);
    const auto tuning = motor.tuning();
    // A reversal at a run: plant (brake on the old heading), turn, push off.
    const Redirect reverse = redirect(.5F, 5, 180);
    check(reverse.pivots == 1 && reverse.pivot_legs, "a reversal at speed is one pivot");
    check(reverse.heading_moved_while_fast == 0.F, "the heading holds while braking into the pivot: " +
                                                       std::to_string(reverse.heading_moved_while_fast));
    check(reverse.worst_brake_error < 1e-4F, "a pivot brakes at the pivot braking, harder than an ordinary stop: " +
                                                 std::to_string(reverse.worst_brake_error));
    check(std::abs(reverse.lowest_speed - tuning.pivot_speed) < 1e-4F,
          "a pivot brakes to the pivot speed, not to a stop or through it: " + std::to_string(reverse.lowest_speed));
    const double braking_distance = (25.0 - static_cast<double>(tuning.pivot_speed) * tuning.pivot_speed) / (2.0 * tuning.pivot_braking);
    check(reverse.overshoot > braking_distance * .9 && reverse.overshoot < braking_distance + 1.0,
          "the body carries on along the old heading for its braking distance: " + std::to_string(reverse.overshoot) + " vs " +
              std::to_string(braking_distance));
    // A turn inside the pivot angle is steered, not pivoted.
    const Redirect steer = redirect(.5F, 5, tuning.pivot_angle - 10.F);
    check(steer.pivots == 0 && !steer.pivot_legs && steer.lowest_speed == 5.F, "a turn inside the pivot angle keeps its speed");
    // Just past it, it pivots.
    check(redirect(.5F, 5, tuning.pivot_angle + 10.F).pivots == 1, "a turn past the pivot angle pivots");
    // Below the pivot speed there is nothing to plant against: it simply turns.
    const Redirect slow = redirect(.5F, tuning.pivot_speed * .8F, 180);
    check(slow.pivots == 0, "a reversal below the pivot speed does not pivot");
    // Changing one's mind mid-pivot back to the old way ends the pivot without a second one.
    settle(motor, want(toward(0, 5)));
    for (int i = 0; i < 3; ++i) {
        motor.step(want(toward(180, 5)), tick);
    }
    check(motor.state().legs == loco::Legs::Pivoting, "the pivot has begun");
    settle(motor, want(toward(0, 5)), 120);
    check(motor.state().pivots == 1 && motor.state().legs == loco::Legs::Moving && motor.state().speed == 5.F &&
              std::abs(wrap(motor.state().heading)) < 1e-4F,
          "going back to the old heading mid-pivot carries on without another pivot");
}

// The largest change of velocity in one step over a fixed-seed storm of requests, against the
// most the limits allow. A slide or a snap would show as a larger change.
void test_no_velocity_snap()
{
    for (const float agility : {0.F, .5F, 1.F}) {
        auto motor = make(agility);
        const auto tuning = motor.tuning();
        std::uint32_t seed = 12345;
        const auto random = [&] {
            seed = seed * 1664525U + 1013904223U;
            return static_cast<float>(seed >> 8U) / 16777216.F;
        };
        loco::Request request = want({});
        float worst = 0;
        Vec3 previous{};
        for (int i = 0; i < 20000; ++i) {
            if (i % 23 == 0) {
                request = random() < .15F ? want({}) : want(toward(random() * 360.F, random() * 9.F));
            }
            const float speed_before = motor.state().speed;
            const auto& state = motor.step(request, tick);
            const float change = length({state.velocity.x - previous.x, 0, state.velocity.z - previous.z});
            const float turn_rate = std::max(tuning.turn_rate_rest, tuning.turn_rate_sprint) * degrees;
            const float allowed = std::max({tuning.acceleration, tuning.braking, tuning.pivot_braking}) * tick +
                                  speed_before * turn_rate * tick;
            // A standing start may pick any direction, at no more than one step of acceleration.
            worst = std::max(worst, change - allowed);
            previous = state.velocity;
        }
        check(worst < 1e-3F, "no step changes the velocity by more than the limits allow (agility " + std::to_string(agility) +
                                 "): " + std::to_string(worst));
    }
}

void test_sharp_versus_heavy()
{
    const loco::Profile profile;
    const auto heavy = *loco::resolve(profile), sharp_default = *loco::resolve(profile);
    (void)heavy;
    (void)sharp_default;
    auto slow = make(0.F), quick = make(1.F);
    check(slow.tuning().acceleration == profile.heavy.acceleration && quick.tuning().acceleration == profile.sharp.acceleration &&
              slow.tuning().pivot_angle == profile.heavy.pivot_angle && quick.tuning().turn_rate_sprint == profile.sharp.turn_rate_sprint,
          "agility 0 is the heavy limits and agility 1 the sharp ones");
    const auto mid = make(.5F).tuning();
    check(std::abs(mid.braking - (profile.heavy.braking + profile.sharp.braking) * .5F) < 1e-5F &&
              std::abs(mid.pivot_angle - (profile.heavy.pivot_angle + profile.sharp.pivot_angle) * .5F) < 1e-4F,
          "agility between is a straight line between them");

    // Idle to a run.
    const auto to_run = [](loco::Motor& motor) {
        return until(motor, want(toward(0, 5)), 600, [](const loco::State& state) { return state.speed == 5.F; });
    };
    const int heavy_start = to_run(slow), sharp_start = to_run(quick);
    // A stop from that run.
    const auto stop = [](loco::Motor& motor) {
        double distance = 0;
        until(motor, want({}), 600, [&](const loco::State& state) {
            distance += static_cast<double>(state.speed) * tick;
            return state.speed == 0.F;
        });
        return distance;
    };
    const double heavy_stop = stop(slow), sharp_stop = stop(quick);
    check(sharp_start * 3 < heavy_start, "sharp reaches a run at least three times sooner: " + std::to_string(sharp_start) + " vs " +
                                             std::to_string(heavy_start) + " steps");
    check(sharp_stop * 3 < heavy_stop && sharp_stop < .5 && heavy_stop > 1.2,
          "sharp stops in a third of the distance: " + std::to_string(sharp_stop) + " vs " + std::to_string(heavy_stop) + " m");
    // A reversal at a run.
    const Redirect heavy_turn = redirect(0, 5, 180), sharp_turn = redirect(1, 5, 180);
    check(sharp_turn.steps_to_new_heading * 2 < heavy_turn.steps_to_new_heading,
          "sharp reverses in under half the time: " + std::to_string(sharp_turn.steps_to_new_heading) + " vs " +
              std::to_string(heavy_turn.steps_to_new_heading) + " steps");
    check(sharp_turn.pivots == 1 && heavy_turn.pivots == 1 && sharp_turn.heading_moved_while_fast == 0.F,
          "both plant before they turn: sharpness never skips the braking");
    check(sharp_turn.overshoot < heavy_turn.overshoot, "sharp is carried less far past the turn");
    // The turning circle at a jog: speed over turn rate.
    const auto circle = [](const loco::Tuning& tuning) {
        const float rate = (tuning.turn_rate_rest + (tuning.turn_rate_sprint - tuning.turn_rate_rest) * (3.2F / 7.F)) * degrees;
        return 3.2F / rate;
    };
    check(circle(quick.tuning()) * 2.F < circle(slow.tuning()), "sharp turns in under half the circle at a jog: " +
                                                                  std::to_string(circle(quick.tuning())) + " vs " +
                                                                  std::to_string(circle(slow.tuning())) + " m");
    // A 90 degree cut at a jog: sharp pivots (its pivot angle is 75), heavy sweeps round.
    check(redirect(1, 3.2F, 90).pivots == 1 && redirect(0, 3.2F, 90).pivots == 0,
          "at a jog a right-angle cut is a pivot for sharp and a sweep for heavy");
}

void test_gaits()
{
    auto motor = make(.5F);
    using loco::Gait;
    check(motor.gait_speed(Gait::Idle) == 0.F && motor.gait_speed(Gait::Walk) == 1.6F && motor.gait_speed(Gait::Sprint) == 7.F,
          "gait speeds come from the profile");
    // Settled at each gait's own speed, the gait is that gait.
    const std::pair<float, Gait> expected[]{{0.F, Gait::Idle}, {1.6F, Gait::Walk}, {3.2F, Gait::Jog}, {5.F, Gait::Run}, {7.F, Gait::Sprint}};
    for (const auto& [speed, gait] : expected) {
        settle(motor, speed > 0 ? want(toward(0, speed)) : want({}));
        check(motor.state().gait == gait, std::string{"settled at "} + std::to_string(speed) + " m/s the gait is " + loco::name(gait) +
                                              ", got " + loco::name(motor.state().gait));
    }
    // The slowest creep is a walk, not an idle that slides.
    settle(motor, want({}));
    settle(motor, want(toward(0, .5F)));
    check(motor.state().gait == Gait::Walk, "a slow creep is a walk");
    // Hovering a little either side of a boundary does not flicker: walk and jog meet at 2.4,
    // and the margin is 15% of the 1.6 gap either side.
    settle(motor, want(toward(0, 2.2F)));
    const Gait held = motor.state().gait;
    int changes = 0;
    for (int i = 0; i < 600; ++i) {
        const Gait before = motor.state().gait;
        motor.step(want(toward(0, i % 20 < 10 ? 2.25F : 2.55F)), tick);
        changes += motor.state().gait != before ? 1 : 0;
    }
    check(held == Gait::Walk && changes == 0, "a speed hovering at a boundary does not flicker the gait: " + std::to_string(changes));
    // Past the margin it changes, and back only past the margin the other way.
    settle(motor, want(toward(0, 2.7F)));
    check(motor.state().gait == Gait::Jog, "past the margin the gait changes");
    settle(motor, want(toward(0, 2.3F)));
    check(motor.state().gait == Gait::Jog, "and stays until past the margin the other way");
    settle(motor, want(toward(0, 2.1F)));
    check(motor.state().gait == Gait::Walk, "then changes back");
    // A hard acceleration from a walk to a sprint steps through gaits no faster than the
    // shortest stay, but a stop goes to idle at once.
    auto quick = make(1.F);
    settle(quick, want(toward(0, 1.6F)));
    int since = 1000, shortest = 1000;
    for (int i = 0; i < 300; ++i) {
        const Gait before = quick.state().gait;
        quick.step(want(toward(0, 7)), tick);
        ++since;
        if (quick.state().gait != before) {
            shortest = std::min(shortest, since);
            since = 0;
        }
    }
    check(quick.state().gait == Gait::Sprint && static_cast<float>(shortest) * tick >= quick.tuning().gait_change_seconds - tick,
          "gaits change no faster than the shortest stay: " + std::to_string(shortest) + " steps");
    const int to_idle = until(quick, want({}), 600, [](const loco::State& state) { return state.gait == Gait::Idle; });
    const int to_stop = 1 + static_cast<int>(7.F / (quick.tuning().braking * tick));
    check(to_idle <= to_stop + 1, "a stop reaches idle as soon as it is still: " + std::to_string(to_idle));
}

void test_air()
{
    auto motor = make(.5F);
    settle(motor, want(toward(0, 5)));
    // Airborne with nothing wanted: the speed is kept.
    for (int i = 0; i < 30; ++i) {
        motor.step(want({}, false), tick);
    }
    check(motor.state().speed == 5.F && motor.state().legs == loco::Legs::Airborne, "in the air the speed is kept");
    // Steering in the air changes the velocity by at most the air control each step.
    Vec3 previous = motor.state().velocity;
    float worst = 0;
    for (int i = 0; i < 30; ++i) {
        const auto& state = motor.step(want(toward(180, 5), false), tick);
        worst = std::max(worst, length({state.velocity.x - previous.x, 0, state.velocity.z - previous.z}));
        previous = state.velocity;
    }
    check(std::abs(worst - motor.profile().air_control * tick) < 1e-4F && motor.state().pivots == 0,
          "air steering is limited to the air control, with no pivot: " + std::to_string(worst));
    // Landing carries on from there.
    motor.step(want(toward(0, 5)), tick);
    check(motor.state().legs != loco::Legs::Airborne, "landing returns to the ground rules");
}

void test_actual_velocity()
{
    // Running into a wall: physics reports no movement, so the motor does not wind up speed.
    auto blocked = make(.5F), free_run = make(.5F);
    for (int i = 0; i < 120; ++i) {
        auto request = want(toward(0, 5));
        request.actual_velocity = Vec3{};
        blocked.step(request, tick);
        free_run.step(want(toward(0, 5)), tick);
    }
    check(blocked.state().speed <= blocked.tuning().acceleration * tick + 1e-5F && free_run.state().speed == 5.F,
          "against a wall the motor does not build speed: " + std::to_string(blocked.state().speed));
    // A faster report than the motor's own speed is not taken up (being pushed is physics' business).
    auto pushed = make(.5F);
    settle(pushed, want(toward(0, 2)));
    auto request = want(toward(0, 2));
    request.actual_velocity = toward(0, 6);
    pushed.step(request, tick);
    check(pushed.state().speed == 2.F, "a faster reported speed does not raise the motor's");
    // A sideways report (sliding along a wall) counts only along the heading.
    request.actual_velocity = toward(60, 2);
    pushed.step(request, tick);
    check(pushed.state().speed < 1.4F && pushed.state().speed > .9F, "only the part along the heading counts: " +
                                                                        std::to_string(pushed.state().speed));
}

void test_facing_and_lean()
{
    auto motor = make(.5F);
    const auto tuning = motor.tuning();
    motor.reset(90.F * degrees);
    check(std::abs(motor.state().facing - 90.F * degrees) < 1e-6F && motor.state().speed == 0.F, "a reset faces the way given");
    // Setting off the other way: the body turns at its facing rate, not at once.
    float previous = motor.state().facing, fastest = 0;
    const int steps = until(motor, want(toward(-90, 3)), 600, [&](const loco::State& state) {
        fastest = std::max(fastest, std::abs(wrap(state.facing - previous)));
        previous = state.facing;
        return std::abs(wrap(state.facing + 90.F * degrees)) < 1e-4F;
    });
    check(std::abs(fastest - tuning.facing_rate * degrees * tick) < 1e-4F && steps > 5,
          "the body turns to face its heading at the facing rate: " + std::to_string(steps) + " steps");
    // Standing still keeps the facing.
    settle(motor, want({}));
    check(std::abs(wrap(motor.state().facing + 90.F * degrees)) < 1e-4F, "standing keeps the facing");
    // Stopping before the body has come round does not freeze it half turned: it finishes.
    motor.reset(90.F * degrees);
    motor.step(want(toward(-90, 3)), tick);
    const float partly = motor.state().facing;
    settle(motor, want({}), 120);
    check(std::abs(wrap(partly - 90.F * degrees)) > 1e-3F && std::abs(wrap(partly + 90.F * degrees)) > 1.F &&
              motor.state().speed == 0.F && std::abs(wrap(motor.state().facing + 90.F * degrees)) < 1e-4F,
          "a turn begun is finished after stopping");

    // Lean: forward when speeding up, back when braking, gone when steady; never past the bound.
    motor.reset();
    float most_forward = 0, most_back = 0, bound = 0;
    for (int i = 0; i < 40; ++i) {
        const auto& state = motor.step(want(toward(0, 7)), tick);
        most_forward = std::max(most_forward, state.lean_forward);
        bound = std::max({bound, std::abs(state.lean_forward), std::abs(state.lean_side)});
    }
    settle(motor, want(toward(0, 7)));
    const float steady = std::abs(motor.state().lean_forward);
    for (int i = 0; i < 40; ++i) {
        const auto& state = motor.step(want({}), tick);
        most_back = std::min(most_back, state.lean_forward);
        bound = std::max(bound, std::abs(state.lean_forward));
    }
    check(most_forward > .05F && most_back < -.05F && steady < 1e-3F, "the body leans into speeding up and braking, and not when steady: " +
                                                                         std::to_string(most_forward) + ", " + std::to_string(most_back));
    // Into a turn: steering left leans left (negative), right leans right.
    motor.reset();
    settle(motor, want(toward(0, 5)));
    float left = 0, right = 0;
    for (int i = 0; i < 20; ++i) {
        left = std::min(left, motor.step(want(toward(60, 5)), tick).lean_side);
        bound = std::max(bound, std::abs(motor.state().lean_side));
    }
    settle(motor, want(toward(60, 5)));
    for (int i = 0; i < 20; ++i) {
        right = std::max(right, motor.step(want(toward(0, 5)), tick).lean_side);
        bound = std::max(bound, std::abs(motor.state().lean_side));
    }
    check(left < -.03F && right > .03F, "the body leans into a turn: " + std::to_string(left) + ", " + std::to_string(right));
    check(bound <= motor.profile().max_lean + 1e-6F, "lean never passes its bound: " + std::to_string(bound));
    // The first step of a start leans only part of the way: it eases in.
    motor.reset();
    const float first = motor.step(want(toward(0, 7)), tick).lean_forward;
    check(first > 0.F && first < most_forward * .5F, "lean eases in: " + std::to_string(first));
}

void test_variants_and_agility()
{
    loco::Profile profile;
    loco::Variant combat;
    combat.name = "combat";
    combat.agility = .9F;
    combat.overrides[6] = 2.5F; // pivot_speed
    profile.variants.push_back(combat);
    profile.base.overrides[0] = 11.F; // acceleration
    std::string error;
    auto motor = loco::Motor::create(profile, error);
    check(motor.has_value(), "a profile with a variant and overrides is valid: " + error);
    if (!motor) {
        return;
    }
    check(motor->tuning().acceleration == 11.F, "a value set directly wins over agility");
    const float base_braking = motor->tuning().braking;
    check(motor->select("combat") && motor->agility() == .9F && motor->tuning().pivot_speed == 2.5F &&
              motor->tuning().braking > base_braking && motor->tuning().acceleration != 11.F,
          "selecting a variant uses its agility and its own overrides");
    check(!motor->select("swimming") && motor->agility() == .9F, "an unknown variant is refused and nothing changes");
    motor->set_agility(7.F);
    check(motor->agility() == 1.F && motor->tuning().braking == profile.sharp.braking && motor->tuning().pivot_speed == 2.5F,
          "agility is clamped, and overrides stay");
    motor->set_agility(std::nanf(""));
    check(motor->agility() == 1.F, "a non-number agility is ignored");
    check(motor->select("") && motor->agility() == .5F && motor->select("default"), "the base variant is selected by no name or its own");
    // A reset keeps the selected variant and the pivot count, and stops dead.
    motor->select("combat");
    settle(*motor, want(toward(0, 5)));
    settle(*motor, want(toward(180, 5)));
    const auto pivots = motor->state().pivots;
    motor->reset(1.F);
    check(pivots == 1 && motor->state().pivots == 1 && motor->state().speed == 0.F && motor->state().gait == loco::Gait::Idle &&
              motor->tuning().pivot_speed == 2.5F && motor->state().lean_forward == 0.F,
          "a reset stops dead and keeps the variant");
    check(loco::resolve(profile, "combat").has_value() && !loco::resolve(profile, "nothing").has_value() &&
              loco::resolve(profile)->acceleration == 11.F,
          "resolve gives a variant's limits by name");
}

void test_facing_and_strafe()
{
    auto motor = make(.5F);
    const auto& profile = motor.profile();
    const auto facing_request = [](Vec3 velocity, float yaw_degrees) {
        loco::Request request = want(velocity);
        request.facing = yaw_degrees * degrees;
        return request;
    };
    // Aiming north (yaw 0) and pushing every way round the circle: the speed kept is 1 forward,
    // strafe_scale sideways, backward_scale back, and changes smoothly in between.
    float previous = -1, worst_jump = 0;
    for (int angle = 0; angle <= 360; angle += 5) {
        motor.reset();
        settle(motor, facing_request(toward(static_cast<float>(angle), 5), 0), 300);
        const float rel = std::abs(wrap(static_cast<float>(angle) * degrees));
        const float along = std::cos(rel);
        const float expected = 5.F * (along >= 0 ? profile.strafe_scale + (1 - profile.strafe_scale) * along
                                                 : profile.strafe_scale + (profile.backward_scale - profile.strafe_scale) * -along);
        check(std::abs(motor.state().speed - expected) < 1e-3F, "aiming, a push at " + std::to_string(angle) + " degrees keeps " +
                                                                    std::to_string(expected) + " m/s: " + std::to_string(motor.state().speed));
        check(std::abs(motor.state().facing) < 1e-4F && motor.state().facing_held, "the body keeps facing where it aims");
        check(std::abs(wrap(motor.state().travel - static_cast<float>(angle) * degrees)) < 1e-3F,
              "the travel angle is the push relative to the facing: " + std::to_string(motor.state().travel / degrees));
        if (previous >= 0) {
            worst_jump = std::max(worst_jump, std::abs(motor.state().speed - previous));
        }
        previous = motor.state().speed;
    }
    check(worst_jump < 5.F * (1 - profile.backward_scale) * .1F, "the scale changes smoothly with the angle: " + std::to_string(worst_jump));
    check(std::abs(profile.strafe_scale - .9F) < 1e-6F && std::abs(profile.backward_scale - .75F) < 1e-6F, "default scales");

    // Turning to a wanted facing goes at the facing rate, while moving another way.
    motor.reset();
    settle(motor, want(toward(0, 3)), 120);
    float before = motor.state().facing, fastest = 0;
    const int steps = until(motor, facing_request(toward(0, 3), 120), 600, [&](const loco::State& state) {
        fastest = std::max(fastest, std::abs(wrap(state.facing - before)));
        before = state.facing;
        return std::abs(wrap(state.facing - 120.F * degrees)) < 1e-4F;
    });
    check(std::abs(fastest - motor.tuning().facing_rate * degrees * tick) < 1e-4F && steps > 3 &&
              std::abs(wrap(motor.state().heading)) < 1e-4F,
          "the body turns to the wanted facing at its rate and the heading is untouched: " + std::to_string(steps) + " steps");
    // Releasing the facing turns the body back to its travel.
    settle(motor, want(toward(0, 3)), 120);
    check(!motor.state().facing_held && std::abs(wrap(motor.state().facing)) < 1e-4F && std::abs(motor.state().travel) < 1e-4F,
          "without a wanted facing the body turns back to its travel");
    // A wanted facing that is not a number is refused like any bad request.
    const loco::State kept = motor.state();
    auto bad = facing_request(toward(0, 3), 0);
    bad.facing = std::nanf("");
    motor.step(bad, tick);
    check(same(kept, motor.state()), "a non-number facing changes nothing");
    // Standing still and aiming: the body turns, nothing moves.
    motor.reset();
    settle(motor, facing_request({}, -70), 120);
    check(motor.state().speed == 0.F && std::abs(wrap(motor.state().facing + 70.F * degrees)) < 1e-4F && motor.state().travel == 0.F,
          "standing and aiming turns the body on the spot");

    // Lean is in the body's frame: strafing right and speeding up leans the body to its right.
    motor.reset();
    float side = 0;
    for (int i = 0; i < 10; ++i) {
        side = std::max(side, motor.step(facing_request(toward(-90, 5), 0), tick).lean_side);
    }
    check(side > .03F && std::abs(motor.state().lean_forward) < 1e-4F, "speeding up to the right while facing ahead leans right: " +
                                                                           std::to_string(side));
}

void test_repeats_and_refusals()
{
    // The same requests give the same states, bit for bit, on separate motors.
    auto a = make(.7F), b = make(.7F);
    std::uint32_t seed = 99;
    const auto random = [&] {
        seed = seed * 1664525U + 1013904223U;
        return static_cast<float>(seed >> 8U) / 16777216.F;
    };
    bool identical = true;
    loco::Request request = want({});
    for (int i = 0; i < 5000; ++i) {
        if (i % 17 == 0) {
            request = want(toward(random() * 360.F, random() * 8.F), random() > .1F);
        }
        identical = identical && same(a.step(request, tick), b.step(request, tick));
    }
    check(identical && a.state().pivots > 3, "two motors given the same requests agree bit for bit");

    // A bad step changes nothing.
    const loco::State before = a.state();
    a.step(request, 0.F);
    a.step(request, -1.F);
    a.step(request, std::nanf(""));
    auto bad = request;
    bad.velocity.x = std::nanf("");
    a.step(bad, tick);
    bad = request;
    bad.actual_velocity = Vec3{0, std::numeric_limits<float>::infinity(), 0};
    a.step(bad, tick);
    check(same(before, a.state()), "a step with a bad length or request changes nothing");
    // The wanted velocity's height is ignored.
    auto flat = make(.5F), tall = make(.5F);
    auto up = want(toward(10, 4));
    up.velocity.y = 50.F;
    check(same(flat.step(want(toward(10, 4)), tick), tall.step(up, tick)) && tall.state().velocity.y == 0.F,
          "the wanted velocity's height is ignored");

    // No allocation in 1,000 steps.
    allocations = 0;
    tracking = true;
    for (int i = 0; i < 1000; ++i) {
        a.step(want(toward(static_cast<float>(i), 6)), tick);
    }
    tracking = false;
    check(allocations == 0, "1,000 steps allocate nothing: " + std::to_string(allocations));
}

void test_data()
{
    std::string error;
    loco::Profile profile;
    check(loco::valid(profile, error), "the default profile is valid: " + error);
    const std::string text = R"({"version":1,"agility":0.3,
        "gaits":{"walk":1.4,"jog":3.0,"run":4.8,"sprint":6.5},"gait_margin":0.2,
        "heavy":{"acceleration":5,"braking":8},"sharp":{"turn_rate_sprint":300},
        "overrides":{"facing_rate":500},
        "air_control":1.5,"max_lean":0.2,"lean_seconds":0.1,"strafe_scale":0.8,"backward_scale":0.6,
        "variants":{"combat":{"agility":0.9,"overrides":{"pivot_angle":60}},"sneak":{}}})";
    check(loco::decode_profile(text, profile, error), "a full profile decodes: " + error);
    check(profile.base.agility == .3F && profile.gait_speeds[0] == 1.4F && profile.gait_speeds[3] == 6.5F && profile.gait_margin == .2F &&
              profile.heavy.acceleration == 5.F && profile.heavy.braking == 8.F && profile.heavy.pivot_braking == 12.F &&
              profile.sharp.turn_rate_sprint == 300.F && profile.base.overrides[7] == 500.F && profile.air_control == 1.5F &&
              profile.max_lean == .2F && profile.lean_seconds == .1F && profile.strafe_scale == .8F && profile.backward_scale == .6F,
          "decoded values are the ones written, and the rest keep their defaults");
    check(profile.variants.size() == 2 && profile.variants[0].name == "combat" && profile.variants[0].agility == .9F &&
              profile.variants[0].overrides[5] == 60.F && profile.variants[1].name == "sneak" && profile.variants[1].agility == .3F,
          "variants decode, and one without an agility takes the profile's");
    check(loco::decode_profile(R"({"version":1})", profile, error) && profile.base.agility == .5F && profile.variants.empty(),
          "the smallest profile is the defaults: " + error);

    const loco::Profile untouched = profile;
    const auto refused = [&](const std::string& json, const std::string& what) {
        loco::Profile target = untouched;
        target.air_control = 77.F;
        std::string why;
        check(!loco::decode_profile(json, target, why) && !why.empty() && target.air_control == 77.F, what + " is refused");
    };
    refused(R"({"agility":0.5})", "a profile without a version");
    refused(R"({"version":2})", "another version");
    refused(R"([1])", "a profile that is not an object");
    refused(R"({"version":1,)", "broken JSON");
    refused(R"({"version":1,"speed":3})", "an unknown key");
    refused(R"({"version":1,"agility":"high"})", "an agility that is not a number");
    refused(R"({"version":1,"agility":1.5})", "an agility above 1");
    refused(R"({"version":1,"agility":0.2,"agility":0.4})", "a duplicate key");
    refused(R"({"version":1,"gaits":{"walk":3,"jog":2}})", "gait speeds that do not rise");
    refused(R"({"version":1,"gaits":{"trot":3}})", "an unknown gait");
    refused(R"({"version":1,"gaits":[1,2,3,4]})", "gaits that are not an object");
    refused(R"({"version":1,"heavy":{"acceleration":0}})", "a zero acceleration");
    refused(R"({"version":1,"heavy":{"grip":1}})", "an unknown limit");
    refused(R"({"version":1,"sharp":{"braking":"hard"}})", "a limit that is not a number");
    refused(R"({"version":1,"sharp":{"pivot_angle":5}})", "a pivot angle below 10 degrees");
    refused(R"({"version":1,"overrides":{"pivot_speed":9}})", "a pivot speed above the sprint speed");
    refused(R"({"version":1,"overrides":{"lean":2}})", "an override out of range");
    refused(R"({"version":1,"max_lean":3})", "a lean bound above 1 radian");
    refused(R"({"version":1,"gait_margin":0.6})", "a gait margin above 0.45");
    refused(R"({"version":1,"strafe_scale":0})", "a strafe scale of 0");
    refused(R"({"version":1,"backward_scale":1.5})", "a backward scale above 1");
    refused(R"({"version":1,"strafe_scale":"slow"})", "a strafe scale that is not a number");
    refused(R"({"version":1,"variants":{"default":{}}})", "a variant named like the base");
    refused(R"({"version":1,"variants":{"":{}}})", "a variant with no name");
    refused(R"({"version":1,"variants":{"combat":{"agility":-1}}})", "a variant agility below 0");
    refused(R"({"version":1,"variants":{"combat":{"speed":1}}})", "an unknown variant key");
    refused(R"({"version":1,"variants":{"combat":3}})", "a variant that is not an object");
    refused(R"({"version":1,"variants":{"combat":{"overrides":{"braking":0}}}})", "a variant override out of range");
    refused(std::string(70000, ' '), "a profile over 64 KiB");

    // The same refusals guard a profile built in code.
    loco::Profile bad;
    bad.gait_speeds[2] = bad.gait_speeds[1];
    check(!loco::Motor::create(bad, error).has_value() && !error.empty(), "a motor is not made from an invalid profile");
    bad = {};
    bad.sharp.turn_rate_rest = std::numeric_limits<float>::infinity();
    check(!loco::valid(bad, error), "a non-finite limit is invalid");
    bad = {};
    bad.variants.push_back({"twin", .5F, {}});
    bad.variants.push_back({"twin", .6F, {}});
    check(!loco::valid(bad, error), "two variants with one name are invalid");
    check(std::string{loco::name(loco::Gait::Jog)} == "jog" && std::string{loco::name(loco::Legs::Pivoting)} == "pivoting",
          "gaits and leg states have names");
}
} // namespace

int main()
{
    test_start_and_stop();
    test_analog_and_cap();
    test_turn_rate_by_speed();
    test_pivot();
    test_no_velocity_snap();
    test_sharp_versus_heavy();
    test_gaits();
    test_air();
    test_actual_velocity();
    test_facing_and_lean();
    test_variants_and_agility();
    test_facing_and_strafe();
    test_repeats_and_refusals();
    test_data();
    if (failures != 0) {
        std::cerr << failures << " locomotion motor assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "locomotion motor passed\n";
    return EXIT_SUCCESS;
}
