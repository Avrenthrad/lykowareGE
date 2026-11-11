// animation.single_support (ADR 0070): walking with one foot planted, the pelvis drops so that leg
// reaches lower ground, while the swing foot keeps its animated place. The stance rig's legs are
// straight, so without the drop any step down is out of reach and the plant is let go.

#include "support/stance_fixture.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
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
namespace a = lykoware::animation;
namespace m = lykoware::math;
namespace p = lykoware::physics;
using V = m::DVec3;
using foot_placement::norm;

int failures = 0;
void check(bool ok, const std::string& why)
{
    if (!ok) {
        std::cerr << "FAIL: " << why << '\n';
        ++failures;
    }
}
std::string text(V v)
{
    return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")";
}

// Ground split at x = 0: the left foot (x = -0.2) stands over a top `drop` metres lower than the
// right foot's (x = 0.2), which is at 0.
std::unique_ptr<p::World> split_ground(double drop)
{
    std::string error;
    auto world = p::World::create({}, error);
    if (!world) {
        std::cerr << "FATAL: " << error << '\n';
        std::exit(EXIT_FAILURE);
    }
    for (const V centre : {V{-1, -.5 - drop, 0}, V{1, -.5, 0}}) {
        p::BodyDesc box;
        box.motion = p::MotionType::Static;
        box.shape = p::BoxShape{{1, .5F, 2}};
        box.position = centre;
        if (!world->create_body(box, error)) {
            std::cerr << "FATAL: " << error << '\n';
            std::exit(EXIT_FAILURE);
        }
    }
    return world;
}

std::array<V, 2> ankles(const a::Skeleton& skeleton, const a::Pose& pose, const foot_placement::Placement& actor)
{
    std::vector<m::Mat4> fk;
    a::global_transforms(skeleton, pose, fk);
    return {foot_placement::to_world(actor, foot_placement::position(fk[3])),
            foot_placement::to_world(actor, foot_placement::position(fk[6]))};
}

lykoware::character::FootSettings walking_settings(bool single)
{
    lykoware::character::FootSettings settings;
    settings.locking = true;
    settings.pelvis = true;
    settings.single_support = single;
    settings.max_plant_distance = 1;
    return settings;
}

const rig_roles::Indices roles{{0, 1, 2, 3, 4, 5, 6}};

// Knees bent 50 degrees with level feet, so each leg can reach 15 cm further down; the actor is
// placed so the ankles rest at the clearance above the higher ground.
a::Pose crouched(const a::Skeleton& skeleton, foot_placement::Placement& actor)
{
    auto pose = a::rest_pose(skeleton);
    constexpr float bend = .436F; // 25 degrees
    for (const std::size_t hip : {std::size_t{1}, std::size_t{4}}) {
        pose[hip].rotation = m::from_axis_angle({1, 0, 0}, bend);
        pose[hip + 1].rotation = m::from_axis_angle({1, 0, 0}, -2 * bend);
        pose[hip + 2].rotation = m::from_axis_angle({1, 0, 0}, bend);
    }
    actor = {};
    actor.feet.y = .03 - ankles(skeleton, pose, actor)[0].y;
    return pose;
}

void test_step_down()
{
    std::string error;
    const auto asset = stance_fixture::make();
    const auto rest = a::rest_pose(asset.skeleton);
    auto world = split_ground(.08);
    foot_placement::Placement actor;
    actor.feet = {0, .03, 0}; // ankles at the clearance above the higher ground
    const foot_placement::PlantPhases left_only{{true, false}};
    const auto animated = ankles(asset.skeleton, rest, actor);

    foot_placement::Controller single;
    check(single.configure(asset.skeleton, roles, error, .03, walking_settings(true)), "single-support controller prepares: " + error);
    a::Pose output;
    double worst_reach = 0;
    double worst_swing = 0;
    bool ran = true;
    for (int i = 0; i < 60; ++i) {
        ran = single.update(rest, actor, *world, .02F, true, output, error, left_only) && ran;
        const auto feet = ankles(asset.skeleton, output, actor);
        worst_reach = std::max(worst_reach, norm(feet[0] - V{-.2, -.05, 0}));
        worst_swing = std::max(worst_swing, norm(feet[1] - animated[1]));
    }
    check(ran && single.stats().planted == 1 && single.stats().released == 0,
          "the left foot plants on the lower ground and is never let go");
    check(worst_reach < 2e-3, "from the first frame the planted foot reaches the lower ground: " + std::to_string(worst_reach));
    check(worst_swing < 1e-4, "the swing foot keeps its animated place: " + std::to_string(worst_swing));
    check(single.pelvis_offset() < -.079 && single.pelvis_offset() > -.09,
          "the pelvis drops by about the step: " + std::to_string(single.pelvis_offset()));
    check(single.stats().single_support >= 60 && single.stats().swing_held >= 60, "single-support frames are counted");

    // Positive control: with two-foot pelvis only, the straight planted leg cannot reach and lets go.
    foot_placement::Controller bilateral;
    check(bilateral.configure(asset.skeleton, roles, error, .03, walking_settings(false)), "two-foot controller prepares");
    for (int i = 0; i < 5; ++i) {
        ran = bilateral.update(rest, actor, *world, .02F, true, output, error, left_only) && ran;
    }
    const auto loose = ankles(asset.skeleton, output, actor);
    check(ran && bilateral.stats().released >= 1 && norm(loose[0] - V{-.2, -.05, 0}) > .05 &&
              bilateral.pelvis_offset() == 0 && bilateral.stats().single_support == 0,
          "without single support the plant is let go: " + text(loose[0]));

    // Mirrored: the right foot planted on lower ground.
    auto mirrored = split_ground(-.08); // the right side is now 8 cm lower
    foot_placement::Placement high;
    high.feet = {0, .11, 0};
    foot_placement::Controller right;
    check(right.configure(asset.skeleton, roles, error, .03, walking_settings(true)), "mirrored controller prepares");
    for (int i = 0; i < 10; ++i) {
        ran = right.update(rest, high, *mirrored, .02F, true, output, error, foot_placement::PlantPhases{{false, true}}) && ran;
    }
    const auto right_feet = ankles(asset.skeleton, output, high);
    check(ran && right.stats().released == 0 && norm(right_feet[1] - V{.2, .03, 0}) < 2e-3,
          "the right foot alone reaches its lower ground too: " + text(right_feet[1]));

    // A step beyond the pelvis bound (0.15 m) still lets go, and the pelvis stays within it.
    auto deep = split_ground(.3);
    foot_placement::Controller bounded;
    check(bounded.configure(asset.skeleton, roles, error, .03, walking_settings(true)), "bounded controller prepares");
    for (int i = 0; i < 10; ++i) {
        ran = bounded.update(rest, actor, *deep, .02F, true, output, error, left_only) && ran;
    }
    check(ran && bounded.stats().released >= 1 && bounded.pelvis_offset() >= -.15,
          "past the bound the plant is let go and the pelvis stays within 0.15 m: " + std::to_string(bounded.pelvis_offset()));
}

void test_bent_legs()
{
    // With bent legs the planted leg reaches the lower ground without any drop, so the pelvis
    // eases down toward the planted foot's own depth (8 cm) by smoothing alone.
    std::string error;
    const auto asset = stance_fixture::make();
    auto world = split_ground(.08);
    foot_placement::Placement actor;
    const auto bent = crouched(asset.skeleton, actor);
    foot_placement::Controller single;
    check(single.configure(asset.skeleton, roles, error, .03, walking_settings(true)), "bent-leg controller prepares");
    a::Pose output;
    bool ran = true;
    for (int i = 0; i < 60; ++i) {
        ran = single.update(bent, actor, *world, .02F, true, output, error, foot_placement::PlantPhases{{true, false}}) && ran;
    }
    check(ran && single.stats().released == 0 && std::abs(single.pelvis_offset() + .08) < 2e-3,
          "with slack in the leg the pelvis eases to the planted foot's depth: " + std::to_string(single.pelvis_offset()));
}

void test_unchanged_elsewhere()
{
    std::string error;
    const auto asset = stance_fixture::make();
    auto world = split_ground(.08);
    foot_placement::Placement actor;
    const auto rest = crouched(asset.skeleton, actor);
    // Both feet planted (the bent legs reach both grounds, so neither lets go), and standing still:
    // single support changes nothing.
    for (const bool walking : {true, false}) {
        foot_placement::Controller with;
        foot_placement::Controller without;
        check(with.configure(asset.skeleton, roles, error, .03, walking_settings(true)) &&
                  without.configure(asset.skeleton, roles, error, .03, walking_settings(false)),
              "comparison controllers prepare");
        a::Pose x;
        a::Pose y;
        bool same = true;
        for (int i = 0; i < 30; ++i) {
            const auto phases = walking ? std::optional{foot_placement::PlantPhases{{true, true}}} : std::nullopt;
            same = with.update(rest, actor, *world, .02F, true, x, error, phases) &&
                   without.update(rest, actor, *world, .02F, true, y, error, phases) && same;
            for (std::size_t j = 0; j < x.size(); ++j) {
                same = same && x[j].translation.x == y[j].translation.x && x[j].translation.y == y[j].translation.y &&
                       x[j].translation.z == y[j].translation.z && x[j].rotation.x == y[j].rotation.x &&
                       x[j].rotation.y == y[j].rotation.y && x[j].rotation.z == y[j].rotation.z &&
                       x[j].rotation.w == y[j].rotation.w;
            }
        }
        check(with.stats().released == 0 && with.stats().pelvis_frames > 0, "both feet stay planted and the pelvis drops");
        check(same && with.stats().single_support == 0,
              walking ? "with both feet planted the result is exactly as before" : "standing still the result is exactly as before");
    }
    // Standing at a ledge with one foot over nothing: still exactly as before.
    std::string ledge_error;
    auto ledge = p::World::create({}, ledge_error);
    p::BodyDesc box;
    box.motion = p::MotionType::Static;
    box.shape = p::BoxShape{{1, .5F, 2}};
    box.position = {1, -.58, 0}; // only the right foot has ground, 8 cm down
    check(ledge && ledge->create_body(box, ledge_error), "the ledge is built");
    foot_placement::Controller with;
    foot_placement::Controller without;
    check(with.configure(asset.skeleton, roles, error, .03, walking_settings(true)) &&
              without.configure(asset.skeleton, roles, error, .03, walking_settings(false)),
          "ledge controllers prepare");
    a::Pose x;
    a::Pose y;
    bool same = true;
    for (int i = 0; i < 30; ++i) {
        same = with.update(rest, actor, *ledge, .02F, true, x, error) && without.update(rest, actor, *ledge, .02F, true, y, error) &&
               same;
        for (std::size_t j = 0; j < x.size(); ++j) {
            same = same && x[j].translation.y == y[j].translation.y && x[j].rotation.x == y[j].rotation.x &&
                   x[j].rotation.w == y[j].rotation.w;
        }
    }
    check(same && with.stats().single_support == 0 && with.stats().swing_held == 0 && with.stats().contacts > 0,
          "standing at a ledge on one foot is exactly as before");
}

void test_allocations()
{
    std::string error;
    const auto asset = stance_fixture::make();
    const auto rest = a::rest_pose(asset.skeleton);
    auto world = split_ground(.08);
    foot_placement::Controller steady;
    check(steady.configure(asset.skeleton, roles, error, .03, walking_settings(true)), "allocation fixture prepares");
    a::Pose output = rest;
    foot_placement::Placement actor;
    actor.feet = {0, .03, 0};
    const foot_placement::PlantPhases left_only{{true, false}};
    check(steady.update(rest, actor, *world, .01F, true, output, error, left_only), "allocation fixture plants");
    bool ok = true;
    allocations = 0;
    tracking = true;
    for (int i = 0; i < 1000; ++i) {
        actor.feet.z = .01 * std::sin(i * .01);
        ok = steady.update(rest, actor, *world, .01F, true, output, error, left_only) && ok;
    }
    tracking = false;
    check(ok && allocations == 0, "1,000 single-support updates allocate nothing: " + std::to_string(allocations));
    check(steady.stats().swing_held >= 1000, "every update held the swing foot");
}

void test_settings()
{
    std::string error;
    lykoware::character::FootSettings settings;
    settings.single_support = true;
    check(!lykoware::character::valid_foot_settings(settings, error), "single support alone is refused");
    settings.pelvis = true;
    check(!lykoware::character::valid_foot_settings(settings, error), "single support without locking is refused");
    settings.pelvis = false;
    settings.locking = true;
    check(!lykoware::character::valid_foot_settings(settings, error), "single support without pelvis is refused");
    settings.pelvis = true;
    check(lykoware::character::valid_foot_settings(settings, error), "single support with pelvis and locking is valid: " + error);

    lykoware::character::Profile profile;
    std::string bones;
    for (const auto* name : rig_roles::names) {
        bones += std::string{bones.empty() ? "" : ","} + "\"" + name + "\":\"" + name + "\"";
    }
    const std::string rig = R"("version":1,"model":"hero.glb","foot_ik":true,"bones":{)" + bones + "}";
    const std::string locking = rig + R"(,"animation_graph":"walk.graph.json","foot_locking":true)";
    const bool decoded = lykoware::character::decode_profile(
        "{" + locking + R"(,"foot_pelvis":true,"foot_pelvis_single_support":true})", "", profile, error);
    check(decoded && profile.foot_settings.single_support, "a profile turns single support on: " + error);
    check(!lykoware::character::decode_profile("{" + locking + R"(,"foot_pelvis_single_support":true})", "", profile, error),
          "a profile's single support needs foot_pelvis");
    check(!lykoware::character::decode_profile("{" + rig + R"(,"foot_pelvis":true,"foot_pelvis_single_support":true})", "",
                                               profile, error),
          "a profile's single support needs foot_locking");
    check(!lykoware::character::decode_profile(
              "{" + locking + R"(,"foot_pelvis":true,"foot_pelvis_single_support":1})", "", profile, error),
          "single support must be a boolean");
}
} // namespace

int main()
{
    test_step_down();
    test_bent_legs();
    test_unchanged_elsewhere();
    test_allocations();
    test_settings();
    if (failures != 0) {
        std::cerr << failures << " single support assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "single support passed\n";
    return EXIT_SUCCESS;
}
