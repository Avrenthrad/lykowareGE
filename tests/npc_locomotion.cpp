// npc.locomotion (ADR 0074, step 5): an NPC moved by the same locomotion motor and physics
// character as the player, with its own agility. It follows waypoints within the motor's
// limits, walks the obstacle course without sinking, ends where an equivalent player ends,
// repeats exactly, and allocates nothing per step.

#include <lykoware/locomotion.hpp>
#include <lykoware/npc.hpp>
#include <lykoware/physics.hpp>
#include <lykoware/player.hpp>

#include "obstacle_course.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <numbers>
#include <optional>
#include <string>
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
namespace physics = lykoware::physics;
namespace loco = lykoware::locomotion;
namespace math = lykoware::math;
using lykoware::NpcCharacter;
using lykoware::NpcConfig;
using lykoware::PlayerCharacter;
using lykoware::PlayerConfig;
using lykoware::PlayerIntent;

int failures = 0;
int checks = 0;
void check(bool ok, const std::string& why)
{
    ++checks;
    if (!ok) {
        std::cerr << "FAIL: " << why << '\n';
        ++failures;
    }
}

constexpr float tick = 1.F / 60.F;
constexpr float pi = std::numbers::pi_v<float>;

std::unique_ptr<physics::World> make_world()
{
    std::string error;
    auto world = physics::World::create({}, error);
    if (!world) {
        std::cerr << "FATAL: world: " << error << '\n';
        std::exit(EXIT_FAILURE);
    }
    return world;
}
void add_box(physics::World& world, math::Vec3 half, math::DVec3 at)
{
    std::string error;
    physics::BodyDesc desc;
    desc.shape = physics::BoxShape{half};
    desc.motion = physics::MotionType::Static;
    desc.position = at;
    if (!world.create_body(desc, error)) {
        std::cerr << "FATAL: body: " << error << '\n';
        std::exit(EXIT_FAILURE);
    }
}
std::unique_ptr<physics::World> flat_world(bool wall = false)
{
    auto world = make_world();
    add_box(*world, {60.F, .5F, 60.F}, {0, -.5, 0});
    if (wall) {
        add_box(*world, {10.F, 2.F, .5F}, {0, 2, -6.5});
    }
    return world;
}
loco::Profile profile_of(float agility)
{
    loco::Profile profile;
    profile.base.agility = agility;
    return profile;
}
std::unique_ptr<NpcCharacter> make_npc(physics::World& world, float agility, math::DVec3 at = {0.0, 0.1, 0.0})
{
    std::string error;
    NpcConfig config;
    config.body.position = at;
    config.locomotion = profile_of(agility);
    auto npc = NpcCharacter::create(world, config, error);
    if (!npc) {
        std::cerr << "FATAL: npc: " << error << '\n';
        std::exit(EXIT_FAILURE);
    }
    return npc;
}
// One fixed step; returns how the feet really moved over it, measured from positions (the
// world reports the velocity it was asked for, which is not evidence).
math::Vec3 step(physics::World& world, NpcCharacter& npc)
{
    const math::DVec3 before = npc.state().position;
    npc.before_step(tick);
    (void)world.step(tick);
    const math::DVec3 after = npc.state().position;
    return {static_cast<float>((after.x - before.x) / static_cast<double>(tick)), 0.F,
            static_cast<float>((after.z - before.z) / static_cast<double>(tick))};
}
void settle(physics::World& world, NpcCharacter& npc)
{
    for (int i = 0; i < 30; ++i) {
        (void)step(world, npc);
    }
}
double horizontal_distance(const math::DVec3& a, const math::DVec3& b)
{
    return std::hypot(a.x - b.x, a.z - b.z);
}

// --- seek -----------------------------------------------------------------------------------

void test_seek()
{
    const math::DVec3 from{1.0, 5.0, 2.0};
    const auto length = [](math::Vec3 v) { return std::hypot(v.x, v.z); };
    math::Vec3 far = lykoware::seek(from, {11.0, -3.0, 2.0}, 4.F, 2.F);
    check(std::abs(far.x - 4.F) < 1e-5F && far.y == 0.F && std::abs(far.z) < 1e-5F,
          "far from the target it goes straight there at full speed, ignoring heights");
    check(std::abs(length(lykoware::seek(from, {4.0, 0.0, 6.0}, 4.F, 2.F)) - 4.F) < 1e-4F, "a diagonal is no faster");
    // Inside the arrive radius it slows in proportion to the distance left.
    const math::Vec3 half = lykoware::seek(from, {2.0, 0.0, 2.0}, 4.F, 2.F);
    check(std::abs(half.x - 2.F) < 1e-5F, "half the arrive radius away it goes at half speed: " + std::to_string(half.x));
    const math::Vec3 near = lykoware::seek(from, {1.2, 0.0, 2.0}, 4.F, 2.F);
    check(length(near) < length(half) && length(near) > 0.F, "and slower still the closer it is");
    check(length(lykoware::seek(from, from, 4.F, 2.F)) == 0.F, "it is zero at the target");
    check(length(lykoware::seek(from, {1.0, 9.0, 2.0}, 4.F, 2.F)) == 0.F, "directly above the target counts as at it");
    // No arrive radius: never slows.
    check(std::abs(length(lykoware::seek(from, {1.1, 0.0, 2.0}, 4.F, 0.F)) - 4.F) < 1e-4F, "without an arrive radius it never slows");
    // Bad inputs.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    check(length(lykoware::seek(from, {11.0, 0.0, 2.0}, 0.F, 2.F)) == 0.F && length(lykoware::seek(from, {11.0, 0.0, 2.0}, -1.F, 2.F)) == 0.F &&
              length(lykoware::seek(from, {11.0, 0.0, 2.0}, nan, 2.F)) == 0.F &&
              length(lykoware::seek(from, {11.0, 0.0, 2.0}, 4.F, nan)) == 0.F &&
              length(lykoware::seek({std::nan(""), 0.0, 0.0}, {1.0, 0.0, 0.0}, 4.F, 2.F)) == 0.F &&
              length(lykoware::seek(from, {1.0, 0.0, std::numeric_limits<double>::infinity()}, 4.F, 2.F)) == 0.F,
          "a non-positive or non-finite input wants nothing");
}

// --- waypoints on flat ground ---------------------------------------------------------------

struct Trip {
    bool reached_all = false;
    int ticks = 0;
    std::uint64_t pivots = 0;
    std::vector<int> reached_at;
    float worst_over = -1e9F; // largest excess of a real velocity change over what the limits allow
    std::uint64_t hash = 0;
    math::DVec3 end;
};

// East 8 m, a 90 degree turn north 8 m, a 180 degree reversal south 8 m, then a 90 degree turn
// west back to the start, where it stops.
const std::vector<math::DVec3>& route()
{
    static const std::vector<math::DVec3> points{{8.0, 0.0, 0.0}, {8.0, 0.0, -8.0}, {8.0, 0.0, 0.0}, {0.0, 0.0, 0.0}};
    return points;
}

Trip run_route(float agility)
{
    auto world = flat_world();
    auto npc = make_npc(*world, agility);
    settle(*world, *npc);
    const loco::Profile profile = profile_of(agility);
    const loco::Tuning tuning = *loco::resolve(profile);
    const float cruise = profile.gait_speeds[2];
    const std::vector<math::DVec3>& points = route();
    Trip trip;
    std::size_t next = 0;
    math::Vec3 previous{};
    while (next < points.size() && trip.ticks < 3000) {
        const bool last = next + 1 == points.size();
        const math::DVec3 feet = npc->state().position;
        if (horizontal_distance(feet, points[next]) < (last ? .3 : .6)) {
            trip.reached_at.push_back(trip.ticks);
            ++next;
            if (next == points.size()) {
                break;
            }
        }
        const bool final_leg = next + 1 == points.size();
        npc->want(lykoware::seek(feet, points[next], cruise, final_leg ? 2.5F : 0.F));
        const float speed_before = std::hypot(previous.x, previous.z);
        const math::Vec3 moved = step(*world, *npc);
        const float change = std::hypot(moved.x - previous.x, moved.z - previous.z);
        const float turn_rate = std::max(tuning.turn_rate_rest, tuning.turn_rate_sprint) * pi / 180.F;
        const float allowed = std::max({tuning.acceleration, tuning.braking, tuning.pivot_braking}) * tick +
                              speed_before * turn_rate * tick;
        trip.worst_over = std::max(trip.worst_over, change - allowed);
        previous = moved;
        ++trip.ticks;
    }
    trip.reached_all = next == points.size();
    trip.pivots = npc->locomotion().pivots;
    trip.hash = world->state_hash();
    trip.end = npc->state().position;
    return trip;
}

// How many of the route's corners are tighter than the profile's pivot angle.
std::uint64_t expected_pivots(float agility)
{
    const loco::Tuning tuning = *loco::resolve(profile_of(agility));
    std::uint64_t count = 0;
    math::DVec3 at{0.0, 0.0, 0.0};
    math::Vec3 heading{};
    for (const math::DVec3& to : route()) {
        const math::Vec3 leg = lykoware::seek(at, to, 1.F, 0.F);
        if (heading.x != 0.F || heading.z != 0.F) {
            const float angle = std::acos(std::clamp(heading.x * leg.x + heading.z * leg.z, -1.F, 1.F)) * 180.F / pi;
            if (angle > tuning.pivot_angle) {
                ++count;
            }
        }
        heading = leg;
        at = to;
    }
    return count;
}

void test_waypoints()
{
    const Trip heavy = run_route(0.F);
    const Trip sharp = run_route(1.F);
    check(heavy.reached_all && heavy.reached_at.size() == route().size(), "the heavy NPC reaches every waypoint");
    check(sharp.reached_all && sharp.reached_at.size() == route().size(), "the sharp NPC reaches every waypoint");
    check(horizontal_distance(heavy.end, {0.0, 0.0, 0.0}) < .35 && horizontal_distance(sharp.end, {0.0, 0.0, 0.0}) < .35,
          "both end at the last waypoint");
    check(sharp.ticks < heavy.ticks, "the sharp NPC finishes sooner: " + std::to_string(sharp.ticks) + " vs " +
                                         std::to_string(heavy.ticks) + " ticks");
    // Neither ever changes its real velocity faster than the motor's limits allow.
    check(heavy.worst_over < .02F, "the heavy NPC stays within the velocity-change limits: " + std::to_string(heavy.worst_over));
    check(sharp.worst_over < .02F, "the sharp NPC stays within the velocity-change limits: " + std::to_string(sharp.worst_over));
    // Pivots as the profiles' data implies: the heavy one pivots only on the reversal, the
    // sharp one on every corner.
    check(expected_pivots(0.F) == 1 && expected_pivots(1.F) == 3, "the route has one corner past the heavy pivot angle, three past the sharp");
    check(heavy.pivots == expected_pivots(0.F), "the heavy NPC pivots as its data says: " + std::to_string(heavy.pivots));
    check(sharp.pivots == expected_pivots(1.F), "the sharp NPC pivots as its data says: " + std::to_string(sharp.pivots));
    // Same inputs twice, same world.
    check(run_route(0.F).hash == heavy.hash && run_route(1.F).hash == sharp.hash, "the same route twice gives the same world state");
    check(heavy.hash != sharp.hash, "and a different agility ends differently");
}

// --- the obstacle course --------------------------------------------------------------------

void test_course()
{
    auto world = make_world();
    std::string error;
    check(obstacle_course::build(*world, error), "the course builds");
    auto npc = make_npc(*world, .5F, obstacle_course::start);
    settle(*world, *npc);
    // The rolling terrain, up the 20 degree ramp, onto the 1.5 m platform.
    const std::vector<math::DVec3> points{{0.0, 0.0, 12.0}, {0.0, 0.0, 6.0}, {0.0, 0.0, 2.0}, {0.0, 0.0, -2.0}};
    std::size_t next = 0;
    int sunk = 0, longest_sunk = 0, ticks = 0;
    double deepest = 0.0;
    while (next < points.size() && ticks < 3000) {
        const math::DVec3 feet = npc->state().position;
        const bool last = next + 1 == points.size();
        if (horizontal_distance(feet, points[next]) < (last ? .3 : .6)) {
            ++next;
            continue;
        }
        npc->want(lykoware::seek(feet, points[next], 4.F, last ? 1.5F : 0.F));
        (void)step(*world, *npc);
        ++ticks;
        const math::DVec3 now = npc->state().position;
        const auto hit = world->cast_ray({now.x, now.y + 0.5, now.z}, {0.F, -1.F, 0.F});
        const double depth = hit ? hit->point.y - now.y : -1.0;
        deepest = std::max(deepest, depth);
        sunk = depth > 0.02 ? sunk + 1 : 0;
        longest_sunk = std::max(longest_sunk, sunk);
    }
    check(next == points.size(), "the NPC walks onto the platform");
    for (int i = 0; i < 90; ++i) {
        npc->want(lykoware::seek(npc->state().position, points.back(), 4.F, 1.5F));
        (void)step(*world, *npc);
    }
    check(std::abs(npc->state().position.y - 1.5) < .05 && npc->state().ground == physics::GroundState::OnGround,
          "and stands on it, 1.5 m up: " + std::to_string(npc->state().position.y));
    check(longest_sunk <= 2, "its feet never stay sunk into the ground: " + std::to_string(longest_sunk) + " ticks, " +
                                 std::to_string(deepest) + " m deepest");
    check(deepest < .02 + .05, "and never go far under it: " + std::to_string(deepest));
}

// --- a player and an NPC are the same controller --------------------------------------------

void test_same_as_player()
{
    for (const float agility : {0.F, .5F, 1.F}) {
        auto player_world = flat_world();
        auto npc_world = flat_world();
        std::string error;
        PlayerConfig player_config;
        player_config.body.position = {0.0, 0.1, 0.0};
        player_config.locomotion = profile_of(agility);
        auto player = PlayerCharacter::create(*player_world, player_config, error);
        auto npc = make_npc(*npc_world, agility);
        check(player != nullptr, "the player is created");
        if (!player) {
            return;
        }
        for (int i = 0; i < 30; ++i) {
            player->before_step(tick);
            (void)player_world->step(tick);
            (void)step(*npc_world, *npc);
        }
        const float run = player->locomotion()->speed; // idle: 0, for the first comparison
        check(run == 0.F && player->state().position.z == npc->state().position.z, "both settle alike");
        const loco::Profile profile = profile_of(agility);
        // A push (forward, right) held for some ticks: the player gets it as an intent, the NPC
        // as the velocity that intent means (the camera stays put, so it is the same velocity).
        struct Push {
            float forward, right;
            int ticks;
        };
        const std::vector<Push> script{{1, 0, 100}, {-1, 0, 100}, {0, 1, 70}, {-1, 0, 80}, {0, 0, 50}, {1, 1, 90}, {0, -1, 60}, {0, 0, 80}};
        bool same_every_tick = true;
        bool same_world = true;
        for (const Push& push : script) {
            PlayerIntent intent;
            intent.forward = push.forward;
            intent.right = push.right;
            player->steer(intent);
            const math::Vec2 move = [&] {
                const float length = std::hypot(push.right, push.forward);
                return length > 1.F ? math::Vec2{push.right / length, push.forward / length} : math::Vec2{push.right, push.forward};
            }();
            const math::Vec3 velocity =
                (player->camera().forward() * move.y + player->camera().right() * move.x) * profile.gait_speeds[2];
            npc->want(velocity);
            for (int i = 0; i < push.ticks; ++i) {
                player->before_step(tick);
                (void)player_world->step(tick);
                (void)step(*npc_world, *npc);
                const physics::CharacterState a = player->state(), b = npc->state();
                const loco::State& ma = *player->locomotion();
                const loco::State& mb = npc->locomotion();
                same_every_tick = same_every_tick && a.position.x == b.position.x && a.position.y == b.position.y &&
                                  a.position.z == b.position.z && ma.speed == mb.speed && ma.facing == mb.facing &&
                                  ma.legs == mb.legs && ma.pivots == mb.pivots;
                same_world = same_world && player_world->state_hash() == npc_world->state_hash();
            }
        }
        const std::string label = " (agility " + std::to_string(agility) + ")";
        check(same_every_tick, "the player and the NPC are in the same place with the same motor state every tick" + label);
        check(same_world, "and their worlds hash the same" + label);
        check(player->facing() == npc->facing() && player->locomotion()->pivots > 0,
              "and face the same way, after pivoting" + label);
    }
}

// --- measured movement: a wall -------------------------------------------------------------

void test_wall()
{
    auto world = flat_world(true);
    auto npc = make_npc(*world, .5F);
    settle(*world, *npc);
    float worst_speed = 0.F;
    for (int i = 0; i < 360; ++i) {
        npc->want({0.F, 0.F, -5.F});
        (void)step(*world, *npc);
        if (i > 240) {
            worst_speed = std::max(worst_speed, npc->locomotion().speed);
        }
    }
    check(npc->state().position.z > -6.0 && npc->state().position.z < -5.5, "the NPC walks up to the wall and stops there: " +
                                                                               std::to_string(npc->state().position.z));
    // The motor continues from what physics really gave, not from what it asked for, so it
    // does not keep building up speed against the wall.
    check(worst_speed < 1.F, "pushing on the wall does not build up speed the body never gets: " + std::to_string(worst_speed));
    // Turning away, it moves off at once rather than after unwinding stored speed.
    int ticks_to_move = 0;
    const double wall_z = npc->state().position.z;
    while (npc->state().position.z <= wall_z + .01 && ticks_to_move < 120) {
        npc->want({0.F, 0.F, 5.F});
        (void)step(*world, *npc);
        ++ticks_to_move;
    }
    check(npc->state().position.z > wall_z + .01 && ticks_to_move < 30, "and it backs away promptly: " + std::to_string(ticks_to_move) + " ticks");
}

// --- in the air ------------------------------------------------------------------------------

void test_airborne()
{
    // A short floor: the NPC runs off its edge and wants to turn back in mid-air, where the
    // motor allows only the air control (the NPC passes whether it stands on ground to it).
    auto world = make_world();
    add_box(*world, {12.F, .5F, 12.F}, {0, -.5, 0});
    auto npc = make_npc(*world, 1.F);
    settle(*world, *npc);
    const float air = profile_of(1.F).air_control;
    float worst = 0.F;
    int air_ticks = 0;
    bool left = false;
    for (int i = 0; i < 400; ++i) {
        const bool was_air = npc->state().ground == physics::GroundState::InAir;
        if (was_air) {
            left = true;
        }
        npc->want(left ? math::Vec3{-5.F, 0.F, 0.F} : math::Vec3{5.F, 0.F, 0.F});
        const math::Vec3 before = npc->locomotion().velocity;
        (void)step(*world, *npc);
        if (was_air) {
            ++air_ticks;
            const math::Vec3 after = npc->locomotion().velocity;
            worst = std::max(worst, std::hypot(after.x - before.x, after.z - before.z));
        }
    }
    check(left && air_ticks > 100, "the NPC runs off the edge and is airborne: " + std::to_string(air_ticks) + " ticks");
    check(worst <= air * tick + 1e-3F, "in the air it changes velocity only at the air control: " + std::to_string(worst) +
                                           " m/s in a step vs " + std::to_string(air * tick));
}

// --- wants, drawing, agility -----------------------------------------------------------------

void test_wants_and_drawing()
{
    // Non-finite wants are ignored: the earlier want stays in force.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    auto clean_world = flat_world();
    auto dirty_world = flat_world();
    auto clean = make_npc(*clean_world, .5F);
    auto dirty = make_npc(*dirty_world, .5F);
    for (int i = 0; i < 90; ++i) {
        clean->want({3.F, 0.F, 0.F}, .5F);
        dirty->want({3.F, 0.F, 0.F}, .5F);
        dirty->want({nan, 0.F, 0.F});
        dirty->want({0.F, inf, 0.F});
        dirty->want({0.F, 0.F, -inf});
        dirty->want({-3.F, 0.F, 0.F}, nan);
        dirty->want({-3.F, 0.F, 0.F}, inf);
        (void)step(*clean_world, *clean);
        (void)step(*dirty_world, *dirty);
    }
    check(clean_world->state_hash() == dirty_world->state_hash() && dirty->state().position.x > 1.0,
          "non-finite wants are ignored and the last good one stays in force");
    check(std::abs(dirty->facing() - .5F) < 1e-4F && dirty->locomotion().facing_held,
          "a wanted facing is held while it moves (the way it faces differs from the way it travels): " + std::to_string(dirty->facing()));

    // A bad step length changes nothing in the motor.
    const loco::State before = dirty->locomotion();
    dirty->before_step(nan);
    dirty->before_step(0.F);
    dirty->before_step(-1.F);
    check(dirty->locomotion().speed == before.speed && dirty->locomotion().pivots == before.pivots &&
              dirty->locomotion().facing == before.facing,
          "a bad step length changes nothing");

    // Drawing between steps.
    auto world = flat_world();
    auto npc = make_npc(*world, 1.F);
    settle(*world, *npc);
    npc->want({0.F, 0.F, -5.F}, 2.F);
    for (int i = 0; i < 20; ++i) {
        (void)step(*world, *npc);
    }
    npc->want({0.F, 0.F, -5.F}, -2.5F); // a new facing to turn to
    const math::DVec3 feet0 = npc->state().position;
    const float facing0 = npc->facing();
    (void)step(*world, *npc);
    const math::DVec3 feet1 = npc->state().position;
    const float facing1 = npc->facing();
    check(feet1.z < feet0.z - .01, "the NPC is moving");
    check(std::abs(facing1 - facing0) > 1e-3F, "and turning to the wanted facing");
    const math::DVec3 drawn0 = npc->drawn_feet(0.F), drawn1 = npc->drawn_feet(1.F), mid = npc->drawn_feet(.5F);
    check(drawn0.z == feet0.z && drawn0.x == feet0.x && drawn0.y == feet0.y, "drawn at alpha 0 the feet are where they were before the step");
    check(drawn1.z == feet1.z && npc->drawn_feet(nan).z == feet1.z && npc->drawn_feet(9.F).z == feet1.z && npc->drawn_feet(-3.F).z == feet0.z,
          "at alpha 1 they are where they are now; alpha is clamped; a non-finite alpha gives now");
    check(std::abs(mid.z - (feet0.z + feet1.z) / 2.0) < 1e-9, "and half way between at alpha .5");
    check(npc->drawn_facing(0.F) == facing0 && std::abs(npc->drawn_facing(1.F) - facing1) < 1e-6F &&
              std::abs(npc->drawn_facing(.5F) - (facing0 + facing1) / 2.F) < 1e-5F && npc->drawn_facing(nan) == facing1,
          "the facing is drawn between steps too");

    // Agility and variants reach the motor.
    check(npc->set_agility(0.F) && !npc->set_agility(nan) && !npc->select_locomotion("combat") && npc->select_locomotion(""),
          "agility and variants are set through the NPC");
    npc->set_agility(0.F);
    check(npc->locomotion().speed >= 0.F, "the NPC's locomotion state is readable");
    auto slow_world = flat_world(), quick_world = flat_world();
    auto slow = make_npc(*slow_world, .5F), quick = make_npc(*quick_world, .5F);
    slow->set_agility(0.F);
    quick->set_agility(1.F);
    slow->want({0.F, 0.F, -7.F});
    quick->want({0.F, 0.F, -7.F});
    for (int i = 0; i < 20; ++i) {
        (void)step(*slow_world, *slow);
        (void)step(*quick_world, *quick);
    }
    check(quick->locomotion().speed > slow->locomotion().speed + 1.F, "a sharper NPC speeds up faster: " +
                                                                           std::to_string(quick->locomotion().speed) + " vs " +
                                                                           std::to_string(slow->locomotion().speed));
}

// --- creation and destruction -----------------------------------------------------------------

void test_lifetime()
{
    auto world = flat_world();
    std::string error;
    NpcConfig config;
    config.body.position = {0.0, 0.1, 0.0};
    config.locomotion.gait_speeds = {3.F, 2.F, 5.F, 7.F}; // not rising
    check(NpcCharacter::create(*world, config, error) == nullptr && !error.empty(), "an invalid profile creates nothing");
    check(world->character_count() == 0, "and leaves the world with no characters");
    config.locomotion = {};
    config.body.radius = 0.F;
    error.clear();
    check(NpcCharacter::create(*world, config, error) == nullptr && !error.empty() && world->character_count() == 0,
          "the world's refusal is passed on");
    config.body.radius = .3F;
    physics::CharacterId id;
    {
        auto npc = NpcCharacter::create(*world, config, error);
        check(npc != nullptr && world->character_count() == 1 && world->is_valid(npc->character()), "an NPC has a character");
        if (npc) {
            id = npc->character();
        }
    }
    check(world->character_count() == 0 && !world->is_valid(id), "destroying the NPC removes its character");
}

// --- no allocation ----------------------------------------------------------------------------

void test_no_allocation()
{
    auto world = flat_world();
    auto npc = make_npc(*world, .5F);
    settle(*world, *npc);
    std::size_t counted = 0;
    for (int i = 0; i < 1000; ++i) {
        const double angle = static_cast<double>(i) * .05;
        tracking = true;
        npc->want(lykoware::seek(npc->state().position, {20.0 * std::cos(angle), 0.0, 20.0 * std::sin(angle)}, 5.F, 2.F),
                  (i / 100) % 2 == 0 ? std::optional<float>{} : std::optional<float>{1.F});
        npc->before_step(tick);
        tracking = false;
        counted = allocations;
        (void)world->step(tick);
    }
    check(counted == 0, "1,000 want and before_step calls allocate nothing: " + std::to_string(counted));
    check(npc->state().position.x != 0.0 || npc->state().position.z != 0.0, "and the NPC moved meanwhile");
}

} // namespace

int main()
{
    test_seek();
    test_waypoints();
    test_course();
    test_same_as_player();
    test_wall();
    test_airborne();
    test_wants_and_drawing();
    test_lifetime();
    test_no_allocation();
    if (failures != 0) {
        std::cerr << failures << " of " << checks << " checks failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "npc.locomotion: " << checks << " checks passed\n";
    return EXIT_SUCCESS;
}
