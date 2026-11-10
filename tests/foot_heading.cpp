// animation.foot_heading (ADR 0068): a planted foot keeps its heading. The engine FootPlant holds
// the animated heading from the moment a foot plants, flattened onto the support's surface and
// carried with the support; the sample controller twists the planted foot about its up axis back to
// it, within a bound, without disturbing the foot's up axis.

#include "support/stance_fixture.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <lykoware/foot_plant.hpp>
#include <new>
#include <numbers>
#include <string>

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
namespace a = lykoware::animation;
namespace m = lykoware::math;
namespace p = lykoware::physics;
using V = m::DVec3;
constexpr float pi = std::numbers::pi_v<float>;

int failures = 0;
void check(bool ok, const std::string& why)
{
    if (!ok) {
        std::cerr << "FAIL: " << why << '\n';
        ++failures;
    }
}
bool near(m::Vec3 a, m::Vec3 b, float tolerance = 1e-5F)
{
    return m::length(a - b) <= tolerance;
}
std::string text(m::Vec3 v)
{
    return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")";
}
// Signed angle from a to b about +Y, in degrees, of their horizontal parts.
double yaw_between(V a, V b)
{
    return std::atan2(a.z * b.x - a.x * b.z, a.x * b.x + a.z * b.z) * 180.0 / std::numbers::pi;
}

std::unique_ptr<p::World> world_with_floor(m::Quat tilt = {})
{
    std::string error;
    auto world = p::World::create({}, error);
    if (!world) {
        std::cerr << "FATAL: " << error << '\n';
        std::exit(EXIT_FAILURE);
    }
    p::BodyDesc floor;
    floor.motion = p::MotionType::Static;
    floor.shape = p::BoxShape{{5, .5F, 5}};
    floor.rotation = tilt;
    floor.position = {0, -.5, 0};
    if (!world->create_body(floor, error)) {
        std::cerr << "FATAL: " << error << '\n';
        std::exit(EXIT_FAILURE);
    }
    return world;
}

void test_engine_heading()
{
    std::string error;
    auto world = world_with_floor();
    auto plant = a::FootPlant::create({}, error);
    a::FootPlantResult result;
    check(plant->update(*world, {}, m::Vec3{1, 0, 0}, true, result, error) && result.acquired && result.heading &&
              near(*result.heading, {1, 0, 0}),
          "a fresh plant holds the animated heading");
    check(plant->update(*world, {}, m::Vec3{0, 0, 1}, true, result, error) && !result.acquired && result.heading &&
              near(*result.heading, {1, 0, 0}),
          "while planted the animated heading is ignored: the held one stays");
    check(plant->update(*world, {}, false, result, error) && !result.heading && !result.locked(),
          "lifting drops the heading");
    check(plant->update(*world, {}, m::Vec3{2, 2, 0}, true, result, error) && result.heading &&
              near(*result.heading, {1, 0, 0}),
          "a heading tilted off the ground is flattened onto the surface and normalised");
    auto sentinel = result;
    check(!plant->update(*world, {}, m::Vec3{std::nanf(""), 0, 0}, true, result, error) &&
              result.heading == sentinel.heading && result.target == sentinel.target,
          "a non-finite heading is refused and the output kept");
    check(!plant->update(*world, {}, m::Vec3{0, 0, 0}, true, result, error), "a zero heading is refused");
    check(plant->update(*world, {}, m::Vec3{0, 0, -1}, true, result, error) && result.heading &&
              near(*result.heading, {1, 0, 0}),
          "a refused call leaves the held heading as it was");
    check(plant->update(*world, {}, false, result, error) &&
              plant->update(*world, {}, m::Vec3{0, 1, 0}, true, result, error) && result.locked() && !result.heading,
          "a heading along the surface normal holds position but no heading");
    plant->reset();
    check(plant->update(*world, {}, true, result, error) && result.locked() && !result.heading,
          "the call without a heading holds none, as before");
    check(plant->update(*world, {.8, 0, 0}, m::Vec3{1, 0, 0}, true, result, error) && result.released &&
              !result.heading,
          "a release (too far) drops the heading");

    // A support that turns about +Y carries the held heading with it.
    auto moving = p::World::create({}, error);
    p::BodyDesc platform;
    platform.motion = p::MotionType::Kinematic;
    platform.shape = p::BoxShape{{5, .5F, 5}};
    platform.position = {0, -.5, 0};
    // Already turned when the foot plants, so support and world coordinates differ.
    platform.rotation = m::from_axis_angle({0, 1, 0}, .7F);
    const auto id = moving->create_body(platform, error);
    auto carried = a::FootPlant::create({.03, 1, 35}, error);
    check(carried->update(*moving, {}, m::Vec3{0, 0, -1}, true, result, error) && result.heading &&
              near(*result.heading, {0, 0, -1}),
          "a heading is held on a kinematic support, as animated");
    const auto start = moving->state(*id)->rotation;
    check(moving->set_velocity(*id, {}, {0, 1.F, 0}), "the support turns");
    bool followed = true;
    for (int i = 0; i < 20; ++i) {
        followed = followed && moving->step(.02F);
        const auto now = moving->state(*id)->rotation;
        const auto expected = m::rotate(now * m::conjugate(start), m::Vec3{0, 0, -1});
        followed = followed && carried->update(*moving, {}, m::Vec3{1, 0, 0}, true, result, error) && result.heading &&
                   near(*result.heading, expected, 1e-4F);
    }
    check(followed, "the held heading turns with the support: " + (result.heading ? text(*result.heading) : "none"));

    // On a tilted support the held heading lies in the surface plane.
    auto tilted = world_with_floor(m::from_axis_angle({1, 0, 0}, .3F));
    auto slope = a::FootPlant::create({}, error);
    check(slope->update(*tilted, {0, 0, 0}, m::Vec3{0, 0, -1}, true, result, error) && result.locked() &&
              result.heading && std::abs(m::dot(*result.heading, result.normal)) < 1e-5F &&
              std::abs(m::length(*result.heading) - 1.F) < 1e-5F,
          "on a slope the held heading is in the surface plane");
}

// The foot's reference axis (model -Z through its posed joint) in world space, made horizontal.
V reference_heading(const a::Skeleton& skeleton, const a::Pose& pose, const foot_placement::Placement& actor,
                    std::size_t foot)
{
    std::vector<m::Mat4> fk;
    a::global_transforms(skeleton, pose, fk);
    const V model = foot_placement::direction(fk[foot], {0, 0, -1});
    V world = foot_placement::rotate(actor.rotation, model);
    world.y = 0;
    return foot_placement::scale(world, 1.0 / foot_placement::norm(world));
}

V foot_up(const a::Skeleton& skeleton, const a::Pose& pose, const foot_placement::Placement& actor, std::size_t foot)
{
    std::vector<m::Mat4> fk;
    a::global_transforms(skeleton, pose, fk);
    return foot_placement::rotate(actor.rotation, foot_placement::direction(fk[foot], {0, 1, 0}));
}

void test_controller_heading()
{
    std::string error;
    const auto asset = stance_fixture::make();
    const auto rest = a::rest_pose(asset.skeleton);
    const rig_roles::Indices roles{{0, 1, 2, 3, 4, 5, 6}};
    auto world = world_with_floor();
    lykoware::character::FootSettings settings;
    settings.locking = true;
    settings.heading = true;
    settings.max_plant_distance = 1;
    const foot_placement::PlantPhases both{{true, true}};

    foot_placement::Controller locked;
    check(locked.configure(asset.skeleton, roles, error, .03, settings), "a heading-locked controller prepares: " + error);
    a::Pose output;
    foot_placement::Placement actor;
    check(locked.update(rest, actor, *world, .01F, true, output, error, both), "both feet plant facing -Z");
    const V planted = reference_heading(asset.skeleton, output, actor, 3);
    check(std::abs(yaw_between({0, 0, -1}, planted)) < 1e-3, "planted, the foot faces where it was animated");

    // The body turns 30 degrees while the feet stay planted: they keep their heading.
    actor.rotation = m::from_axis_angle({0, 1, 0}, 30.F * pi / 180.F);
    check(locked.update(rest, actor, *world, .01F, true, output, error, both), "the body turns over planted feet");
    const double left = yaw_between({0, 0, -1}, reference_heading(asset.skeleton, output, actor, 3));
    const double right = yaw_between({0, 0, -1}, reference_heading(asset.skeleton, output, actor, 6));
    check(std::abs(left) < .05 && std::abs(right) < .05,
          "planted feet keep their heading as the body turns 30 degrees: " + std::to_string(left) + ", " +
              std::to_string(right));
    check(locked.stats().headed >= 2 && locked.stats().heading_clamped == 0, "both feet were turned back, unclamped");

    foot_placement::Controller free;
    auto loose = settings;
    loose.heading = false;
    check(free.configure(asset.skeleton, roles, error, .03, loose), "a controller without heading lock prepares");
    a::Pose unlocked;
    check(free.update(rest, {}, *world, .01F, true, unlocked, error, both) &&
              free.update(rest, actor, *world, .01F, true, unlocked, error, both),
          "without heading lock the same turn runs");
    const double turned = yaw_between({0, 0, -1}, reference_heading(asset.skeleton, unlocked, actor, 3));
    // Leg IK alone twists the foot a fraction of a degree, so this control allows half a degree.
    check(std::abs(std::abs(turned) - 30.0) < .5,
          "without heading lock the planted foot turns with the body: " + std::to_string(turned));

    // A 100 degree turn is more than the 60 degree bound: the foot keeps 60 of it.
    foot_placement::Controller bounded;
    check(bounded.configure(asset.skeleton, roles, error, .03, settings), "bounded controller prepares");
    check(bounded.update(rest, {}, *world, .01F, true, output, error, both), "bounded feet plant");
    actor.rotation = m::from_axis_angle({0, 1, 0}, 100.F * pi / 180.F);
    check(bounded.update(rest, actor, *world, .01F, true, output, error, both), "a 100 degree turn runs");
    const double kept = yaw_between({0, 0, -1}, reference_heading(asset.skeleton, output, actor, 3));
    check(std::abs(std::abs(kept) - 40.0) < .5 && bounded.stats().heading_clamped >= 1,
          "past the bound the foot twists at most 60 degrees back (about 40 of 100 remain): " + std::to_string(kept));

    // Lifting and planting again takes the new heading.
    check(bounded.update(rest, actor, *world, .01F, true, output, error, foot_placement::PlantPhases{{false, false}, {1, 1}}) &&
              bounded.update(rest, actor, *world, .01F, true, output, error, foot_placement::PlantPhases{{true, true}, {2, 2}}),
          "a new step lifts and replants");
    const double replanted = yaw_between({0, 0, -1}, reference_heading(asset.skeleton, output, actor, 3));
    check(std::abs(std::abs(replanted) - 100.0) < .05, "a new plant takes the body's heading: " + std::to_string(replanted));

    // With normal alignment on a slope, the heading twist keeps the foot's up axis on the normal.
    auto slope = world_with_floor(m::from_axis_angle({1, 0, 0}, .2F));
    auto aligned_settings = settings;
    aligned_settings.align_normals = true;
    foot_placement::Controller aligned;
    check(aligned.configure(asset.skeleton, roles, error, .03, aligned_settings), "aligned controller prepares");
    foot_placement::Placement level;
    for (int i = 0; i < 60; ++i) {
        check(aligned.update(rest, level, *slope, .02F, true, output, error, both), "aligned feet settle on the slope");
    }
    level.rotation = m::from_axis_angle({0, 1, 0}, 20.F * pi / 180.F);
    for (int i = 0; i < 60; ++i) {
        check(aligned.update(rest, level, *slope, .02F, true, output, error, both), "aligned body turns on the slope");
    }
    const auto normal = m::rotate(m::from_axis_angle({1, 0, 0}, .2F), m::Vec3{0, 1, 0});
    const V up = foot_up(asset.skeleton, output, level, 3);
    const m::Vec3 up_f{static_cast<float>(up.x), static_cast<float>(up.y), static_cast<float>(up.z)};
    check(near(m::normalize(up_f), normal, 2e-3F), "the heading twist keeps the foot on the surface normal: " + text(up_f));
    check(aligned.stats().headed > 0 && aligned.stats().oriented > 0, "both normal alignment and heading ran");

    // 1,000 heading-locked updates allocate nothing after setup.
    foot_placement::Controller steady;
    check(steady.configure(asset.skeleton, roles, error, .03, settings), "allocation fixture prepares");
    a::Pose steady_out = rest;
    check(steady.update(rest, {}, *world, .01F, true, steady_out, error, both), "allocation fixture plants");
    foot_placement::Placement swaying;
    bool ok = true;
    allocations = 0;
    tracking = true;
    for (int i = 0; i < 1000; ++i) {
        swaying.rotation = m::from_axis_angle({0, 1, 0}, .3F * std::sin(static_cast<float>(i) * .01F));
        ok = steady.update(rest, swaying, *world, .01F, true, steady_out, error, both) && ok;
    }
    tracking = false;
    check(ok && allocations == 0, "1,000 heading-locked updates allocate nothing: " + std::to_string(allocations));
    check(steady.stats().headed >= 2000, "every update turned both feet");
}

void test_settings()
{
    std::string error;
    lykoware::character::FootSettings settings;
    settings.heading = true;
    check(!lykoware::character::valid_foot_settings(settings, error), "heading lock without foot locking is refused");
    settings.locking = true;
    check(lykoware::character::valid_foot_settings(settings, error), "heading lock with foot locking is valid: " + error);
    for (const double bad : {-1.0, 91.0, std::numeric_limits<double>::infinity()}) {
        settings.max_heading_degrees = bad;
        check(!lykoware::character::valid_foot_settings(settings, error), "a heading bound outside 0..90 is refused");
    }
    lykoware::character::Profile profile;
    std::string bones;
    for (const auto* name : rig_roles::names) {
        bones += std::string{bones.empty() ? "" : ","} + "\"" + name + "\":\"" + name + "\"";
    }
    const std::string rig = R"("version":1,"model":"hero.glb","foot_ik":true,"bones":{)" + bones + "}";
    const std::string base = rig + R"(,"animation_graph":"walk.graph.json","foot_locking":true)";
    const bool decoded = lykoware::character::decode_profile(
        "{" + base + R"(,"foot_heading_lock":true,"foot_max_heading_degrees":45})", "", profile, error);
    check(decoded && profile.foot_settings.heading && profile.foot_settings.max_heading_degrees == 45,
          "a profile turns heading lock on with its bound: " + error);
    check(!lykoware::character::decode_profile("{" + rig + R"(,"foot_heading_lock":true})", "", profile, error),
          "a profile's heading lock needs foot locking");
    check(!lykoware::character::decode_profile("{" + base + R"(,"foot_max_heading_degrees":45})", "", profile, error),
          "a heading bound needs heading lock");
    check(!lykoware::character::decode_profile("{" + base + R"(,"foot_heading_lock":"yes"})", "", profile, error),
          "heading lock must be a boolean");
}
} // namespace

int main()
{
    test_engine_heading();
    test_controller_heading();
    test_settings();
    if (failures != 0) {
        std::cerr << failures << " foot heading assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "foot heading passed\n";
    return EXIT_SUCCESS;
}
