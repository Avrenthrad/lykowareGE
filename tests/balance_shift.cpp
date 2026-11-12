// animation.balance_shift (ADR 0072): in single support on lower ground the pelvis also slides
// sideways toward the planted foot, as far as it dropped, within a bound and never past the foot.
// Flat ground, two-foot support and standing still are unchanged.

#include "support/rig_variants.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
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
namespace a = lykoware::animation;
namespace m = lykoware::math;
namespace p = lykoware::physics;
using V = m::DVec3;
using foot_placement::norm;
using Settings = lykoware::character::FootSettings;

int failures = 0;
void check(bool ok, const std::string& why)
{
    if (!ok) {
        std::cerr << "FAIL: " << why << '\n';
        ++failures;
    }
}

constexpr double clearance = .03;
constexpr float tick = .02F;

// Ground split at x = 0. The world -X half is `drop` metres lower when `low_side` is -1, the +X
// half when it is +1; with a zero drop the ground is flat.
std::unique_ptr<p::World> ground(double drop, int low_side = -1)
{
    std::string error;
    auto world = p::World::create({}, error);
    if (!world) {
        std::cerr << "FATAL: " << error << '\n';
        std::exit(EXIT_FAILURE);
    }
    const auto box = [&](V centre) {
        p::BodyDesc desc;
        desc.motion = p::MotionType::Static;
        desc.shape = p::BoxShape{m::Vec3{2.5F, .5F, 5}};
        desc.position = centre;
        if (!world->create_body(desc, error)) {
            std::cerr << "FATAL: " << error << '\n';
            std::exit(EXIT_FAILURE);
        }
    };
    box({-2.5, -.5 - (low_side < 0 ? drop : 0), 0});
    box({2.5, -.5 - (low_side > 0 ? drop : 0), 0});
    return world;
}

struct Rig {
    rig_variants::Rig rig;
    a::Pose rest;
};
Rig make(double hip_half_width = .2, double leg = 1.6)
{
    Rig r{rig_variants::make({"test", leg, .5, hip_half_width, 0, false, false, 1}), {}};
    r.rest = a::rest_pose(r.rig.skeleton);
    return r;
}
V joint(const Rig& r, const a::Pose& pose, const foot_placement::Placement& actor, std::size_t role)
{
    std::vector<m::Mat4> fk;
    a::global_transforms(r.rig.skeleton, pose, fk);
    return foot_placement::to_world(actor, foot_placement::position(fk[r.rig.roles[role]]));
}
foot_placement::Placement standing()
{
    foot_placement::Placement actor;
    actor.feet = {0, clearance, 0};
    return actor;
}
Settings walking(bool shift = true)
{
    Settings settings;
    settings.locking = true;
    settings.pelvis = true;
    settings.single_support = true;
    settings.balance_shift = shift;
    settings.max_plant_distance = 1;
    return settings;
}
bool same(const a::Pose& x, const a::Pose& y)
{
    if (x.size() != y.size()) {
        return false;
    }
    for (std::size_t i = 0; i < x.size(); ++i) {
        const auto &s = x[i], &t = y[i];
        if (s.translation.x != t.translation.x || s.translation.y != t.translation.y ||
            s.translation.z != t.translation.z || s.rotation.x != t.rotation.x || s.rotation.y != t.rotation.y ||
            s.rotation.z != t.rotation.z || s.rotation.w != t.rotation.w) {
            return false;
        }
    }
    return true;
}

struct Run {
    double shift = 0;        // the controller's signed shift at the end
    double first = 0;        // after the first update
    double pelvis_x = 0;     // how far the drawn pelvis moved along world X
    double reach = 0;        // worst planted-foot height error
    double swing = 0;        // worst swing-foot movement
    bool ran = true;
    foot_placement::Stats stats;
};
// Walks `frames` updates with one foot planted (0 left, 1 right) on ground lowered under `low_side`.
Run walk(const Rig& r, Settings settings, double drop, std::size_t planted, int low_side,
         foot_placement::Placement actor = standing(), int frames = 80)
{
    Run run;
    std::string error;
    auto world = ground(drop, low_side);
    foot_placement::Controller controller;
    if (!controller.configure(r.rig.skeleton, r.rig.roles, error, clearance, settings)) {
        check(false, "the controller prepares: " + error);
        run.ran = false;
        return run;
    }
    const foot_placement::PlantPhases phases{{planted == 0, planted == 1}};
    const std::size_t planted_role = planted == 0 ? 3 : 6, swing_role = planted == 0 ? 6 : 3;
    a::Pose out;
    for (int i = 0; i < frames; ++i) {
        if (!controller.update(r.rest, actor, *world, tick, true, out, error, phases) || out.size() != r.rest.size()) {
            check(false, "the update ran: " + error);
            run.ran = false;
            return run;
        }
        if (i == 0) {
            run.first = controller.balance_shift();
        }
        run.reach = std::max(run.reach, std::abs(joint(r, out, actor, planted_role).y - (clearance - drop)));
        run.swing = std::max(run.swing, norm(joint(r, out, actor, swing_role) - joint(r, r.rest, actor, swing_role)));
    }
    run.shift = controller.balance_shift();
    run.pelvis_x = joint(r, out, actor, 0).x - joint(r, r.rest, actor, 0).x;
    run.stats = controller.stats();
    return run;
}

void test_shift()
{
    const Rig r = make();
    // The left foot (world -X) planted 8 cm down: the pelvis moves 4 cm (the default bound) toward it.
    const Run left = walk(r, walking(), .08, 0, -1);
    check(left.ran && std::abs(left.shift + .04) < 1e-3 && std::abs(left.pelvis_x + .04) < 1e-3,
          "the pelvis shifts 4 cm toward the planted left foot: " + std::to_string(left.shift) + ", drawn " +
              std::to_string(left.pelvis_x));
    check(left.reach < 2e-3 && left.stats.released == 0, "the planted foot still reaches and is not let go: " +
                                                             std::to_string(left.reach));
    check(left.swing < 1e-4, "the swing foot keeps its animated place: " + std::to_string(left.swing));
    check(left.stats.balance_frames >= 79 && left.stats.single_support >= 79, "the shift is counted every frame");
    // Smoothed, not snapped: the first update moves only part of the way.
    check(left.first < -1e-3 && left.first > -.012, "the shift eases in: " + std::to_string(left.first));

    // Mirrored: the right foot planted on a lower right side.
    const Run right = walk(r, walking(), .08, 1, 1);
    check(right.ran && std::abs(right.shift - .04) < 1e-3 && std::abs(right.pelvis_x - .04) < 1e-3,
          "mirrored, the pelvis shifts toward the planted right foot: " + std::to_string(right.shift));
    check(right.reach < 2e-3 && right.swing < 1e-4 && right.stats.released == 0, "mirrored, both feet hold");

    // Positive control: without the key the pelvis drops but does not move sideways.
    const Run off = walk(r, walking(false), .08, 0, -1);
    check(off.ran && off.shift == 0 && std::abs(off.pelvis_x) < 1e-6 && off.stats.balance_frames == 0 &&
              off.stats.single_support >= 79,
          "without the key there is no sideways move: " + std::to_string(off.pelvis_x));
}

void test_limits()
{
    const Rig r = make();
    // No farther than the pelvis dropped: a 2 cm step gives a 2 cm shift.
    const Run shallow = walk(r, walking(), .02, 0, -1);
    check(shallow.ran && std::abs(shallow.shift + .02) < 1.5e-3, "the shift is no more than the drop: " +
                                                                     std::to_string(shallow.shift));
    // No farther than the bound.
    Settings tight = walking();
    tight.max_balance_shift = .01;
    const Run bounded = walk(r, tight, .08, 0, -1);
    check(bounded.ran && std::abs(bounded.shift + .01) < 5e-4, "the shift respects its bound: " +
                                                                   std::to_string(bounded.shift));
    // Never past the foot: hips 2 cm either side of the pelvis give a 2 cm shift at most.
    Settings wide = walking();
    wide.max_balance_shift = .1;
    const Rig narrow = make(.02);
    const Run close = walk(narrow, wide, .08, 0, -1);
    check(close.ran && std::abs(close.shift + .02) < 1e-3, "the shift stops over the foot: " + std::to_string(close.shift));
    check(close.reach < 2e-3 && close.stats.released == 0, "over the foot, the planted foot still reaches");
    // The sideways move takes the hip out from above the foot, so a straight leg needs the pelvis
    // a little lower still. Short legs and a wide shift make that 8 mm: without it the plant is let go.
    const Rig small = make(.2, .5);
    const Run far = walk(small, wide, .09, 0, -1);
    check(far.ran && std::abs(far.shift + .09) < 1.5e-3 && far.reach < 2e-3 && far.stats.released == 0,
          "a short straight leg still reaches after a wide shift: shift " + std::to_string(far.shift) + ", reach " +
              std::to_string(far.reach) + ", released " + std::to_string(far.stats.released));
    // A zero bound gives no shift at all.
    Settings none = walking();
    none.max_balance_shift = 0;
    const Run zero = walk(r, none, .08, 0, -1);
    check(zero.ran && zero.shift == 0 && zero.stats.balance_frames == 0, "a zero bound gives no shift");
}

void test_turned_actor()
{
    // Turned half round, the model's right foot stands on the world -X side. Planted there on
    // lower ground, the pelvis moves toward it: along the actor's own +X, which is world -X.
    const Rig r = make();
    auto actor = standing();
    actor.rotation = m::from_axis_angle({0, 1, 0}, std::numbers::pi_v<float>);
    const Run run = walk(r, walking(), .08, 1, -1, actor);
    check(run.ran && std::abs(run.shift - .04) < 1e-3 && std::abs(run.pelvis_x + .04) < 1e-3,
          "the shift follows the actor's own sideways axis: " + std::to_string(run.shift) + ", drawn " +
              std::to_string(run.pelvis_x));
    check(run.reach < 2e-3 && run.swing < 1e-4 && run.stats.released == 0, "turned, both feet hold");
}

void test_ease_out()
{
    const Rig r = make();
    std::string error;
    auto world = ground(.08);
    foot_placement::Controller controller;
    if (!controller.configure(r.rig.skeleton, r.rig.roles, error, clearance, walking())) {
        check(false, "the controller prepares: " + error);
        return;
    }
    const auto actor = standing();
    a::Pose out;
    bool ran = true;
    for (int i = 0; i < 80; ++i) {
        ran = controller.update(r.rest, actor, *world, tick, true, out, error, foot_placement::PlantPhases{{true, false}}) && ran;
    }
    const double engaged = controller.balance_shift();
    // Both feet planted: single support ends, and the shift eases back to exactly nothing.
    double previous = std::abs(engaged);
    bool shrinking = true;
    int frames_to_zero = -1;
    for (int i = 0; i < 400 && ran; ++i) {
        ran = controller.update(r.rest, actor, *world, tick, true, out, error, foot_placement::PlantPhases{{true, true}});
        const double now = std::abs(controller.balance_shift());
        shrinking = shrinking && now <= previous;
        previous = now;
        if (now == 0 && frames_to_zero < 0) {
            frames_to_zero = i;
        }
    }
    check(ran && std::abs(engaged + .04) < 1e-3 && shrinking && controller.balance_shift() == 0 && frames_to_zero > 3,
          "the shift eases out to nothing after single support: took " + std::to_string(frames_to_zero) + " frames");
    // Both feet lifted: the shift eases out, and while it does both feet keep their animated places.
    for (int i = 0; i < 80 && ran; ++i) {
        ran = controller.update(r.rest, actor, *world, tick, true, out, error, foot_placement::PlantPhases{{true, false}, {1, 1}});
    }
    const double again = controller.balance_shift();
    double moved = 0;
    int eased = 0;
    for (int i = 0; i < 400 && ran; ++i) {
        ran = controller.update(r.rest, actor, *world, tick, true, out, error, foot_placement::PlantPhases{{false, false}, {2, 2}});
        if (controller.balance_shift() != 0) {
            ++eased;
            for (const std::size_t role : {std::size_t{3}, std::size_t{6}}) {
                moved = std::max(moved, norm(joint(r, out, actor, role) - joint(r, r.rest, actor, role)));
            }
        }
    }
    check(ran && std::abs(again + .04) < 1e-3 && eased > 3 && controller.balance_shift() == 0 && moved < 1e-3,
          "with both feet lifted the shift eases out and the feet stay where animated (straight legs: within 1 mm): " + std::to_string(moved));
    // Short straight legs and a wide shift: when the other foot plants too, the easing shift
    // must not pull either plant out of reach.
    {
        const Rig small = make(.2, .5);
        Settings wide = walking();
        wide.max_balance_shift = .1;
        auto low = ground(.09);
        foot_placement::Controller short_legs;
        bool ok = short_legs.configure(small.rig.skeleton, small.rig.roles, error, clearance, wide);
        a::Pose pose;
        for (int i = 0; i < 80 && ok; ++i) {
            ok = short_legs.update(small.rest, actor, *low, tick, true, pose, error, foot_placement::PlantPhases{{true, false}});
        }
        const double wide_shift = short_legs.balance_shift();
        double worst = 0;
        for (int i = 0; i < 400 && ok; ++i) {
            ok = short_legs.update(small.rest, actor, *low, tick, true, pose, error, foot_placement::PlantPhases{{true, true}});
            worst = std::max(worst, std::abs(joint(small, pose, actor, 3).y - (clearance - .09)));
            worst = std::max(worst, std::abs(joint(small, pose, actor, 6).y - clearance));
        }
        check(ok && std::abs(wide_shift + .09) < 1.5e-3 && short_legs.balance_shift() == 0 &&
                  short_legs.stats().released == 0 && worst < 2e-3,
              "easing out a wide shift in two-foot support lets no plant go: released " +
                  std::to_string(short_legs.stats().released) + ", worst reach " + std::to_string(worst));
    }
    // A reset clears it at once.
    for (int i = 0; i < 40 && ran; ++i) {
        ran = controller.update(r.rest, actor, *world, tick, true, out, error, foot_placement::PlantPhases{{true, false}, {3, 3}});
    }
    const bool was_engaged = controller.balance_shift() != 0;
    controller.reset();
    check(ran && was_engaged && controller.balance_shift() == 0, "a reset clears the shift");
}

// The same updates with and without the key; true when every pose matches bit for bit.
bool unchanged(const Rig& r, double drop, std::optional<foot_placement::PlantPhases> phases, std::uint64_t& balance_frames)
{
    std::string error;
    auto world_on = ground(drop), world_off = ground(drop);
    foot_placement::Controller on, off;
    if (!on.configure(r.rig.skeleton, r.rig.roles, error, clearance, walking(true)) ||
        !off.configure(r.rig.skeleton, r.rig.roles, error, clearance, walking(false))) {
        return false;
    }
    const auto actor = standing();
    a::Pose with, without;
    for (int i = 0; i < 60; ++i) {
        if (!on.update(r.rest, actor, *world_on, tick, true, with, error, phases) ||
            !off.update(r.rest, actor, *world_off, tick, true, without, error, phases) || !same(with, without)) {
            return false;
        }
    }
    balance_frames = on.stats().balance_frames;
    return true;
}

void test_unchanged_elsewhere()
{
    const Rig r = make();
    std::uint64_t frames = 1;
    check(unchanged(r, 0, foot_placement::PlantPhases{{true, false}}, frames) && frames == 0,
          "flat-ground single support is unchanged by the key");
    frames = 1;
    check(unchanged(r, .08, foot_placement::PlantPhases{{true, true}}, frames) && frames == 0,
          "two-foot support across a step is unchanged by the key");
    frames = 1;
    check(unchanged(r, .08, std::nullopt, frames) && frames == 0, "standing still across a step is unchanged by the key");
}

void test_allocations()
{
    const Rig r = make();
    std::string error;
    auto world = ground(.08);
    foot_placement::Controller controller;
    if (!controller.configure(r.rig.skeleton, r.rig.roles, error, clearance, walking())) {
        check(false, "the controller prepares: " + error);
        return;
    }
    const auto actor = standing();
    const foot_placement::PlantPhases phases{{true, false}};
    a::Pose out;
    bool ok = controller.update(r.rest, actor, *world, tick, true, out, error, phases);
    allocations = 0;
    tracking = true;
    for (int i = 0; i < 1000; ++i) {
        ok = controller.update(r.rest, actor, *world, tick, true, out, error, phases) && ok;
    }
    tracking = false;
    check(ok && allocations == 0, "1,000 shifted updates allocate nothing: " + std::to_string(allocations));
    check(controller.stats().balance_frames >= 1000, "every update shifted");
}

void test_settings()
{
    std::string error;
    Settings settings;
    settings.balance_shift = true;
    check(!lykoware::character::valid_foot_settings(settings, error), "the shift alone is refused");
    settings.pelvis = true;
    settings.locking = true;
    check(!lykoware::character::valid_foot_settings(settings, error), "the shift without single support is refused");
    settings.single_support = true;
    check(lykoware::character::valid_foot_settings(settings, error), "the shift with single support is valid: " + error);
    settings.max_balance_shift = .11;
    check(!lykoware::character::valid_foot_settings(settings, error), "a bound above 10 cm is refused");
    settings.max_balance_shift = -.01;
    check(!lykoware::character::valid_foot_settings(settings, error), "a negative bound is refused");
    settings.max_balance_shift = std::nan("");
    check(!lykoware::character::valid_foot_settings(settings, error), "a bound that is not a number is refused");
    settings.max_balance_shift = .1;
    check(lykoware::character::valid_foot_settings(settings, error), "a 10 cm bound is valid: " + error);

    lykoware::character::Profile profile;
    std::string bones;
    for (const auto* name : rig_roles::names) {
        bones += std::string{bones.empty() ? "" : ","} + "\"" + name + "\":\"" + name + "\"";
    }
    const std::string rig = R"("version":1,"model":"hero.glb","foot_ik":true,"bones":{)" + bones + "}";
    const std::string pelvis = rig + R"(,"animation_graph":"walk.graph.json","foot_locking":true,"foot_pelvis":true)";
    const std::string single = pelvis + R"(,"foot_pelvis_single_support":true)";
    const auto decode = [&](const std::string& body) {
        return lykoware::character::decode_profile("{" + body + "}", "", profile, error);
    };
    check(decode(single + R"(,"foot_balance_shift":true)") && profile.foot_settings.balance_shift &&
              profile.foot_settings.max_balance_shift == .04,
          "a profile turns the shift on with the default bound: " + error);
    check(decode(single + R"(,"foot_balance_shift":true,"foot_max_balance_shift":0.06)") &&
              profile.foot_settings.max_balance_shift == .06,
          "a profile sets the bound: " + error);
    check(decode(single) && !profile.foot_settings.balance_shift, "the shift is off unless asked for");
    check(!decode(pelvis + R"(,"foot_balance_shift":true)"), "a profile's shift needs single support");
    check(!decode(single + R"(,"foot_balance_shift":1)"), "the shift must be a boolean");
    check(!decode(single + R"(,"foot_max_balance_shift":0.02)"), "the bound needs the shift");
    check(!decode(single + R"(,"foot_balance_shift":false,"foot_max_balance_shift":0.02)"), "the bound needs the shift on");
    check(!decode(single + R"(,"foot_balance_shift":true,"foot_max_balance_shift":"wide")"), "the bound must be a number");
    check(!decode(single + R"(,"foot_balance_shift":true,"foot_max_balance_shift":0.2)"), "a profile's bound above 10 cm is refused");
}
} // namespace

int main()
{
    test_shift();
    test_limits();
    test_turned_actor();
    test_ease_out();
    test_unchanged_elsewhere();
    test_allocations();
    test_settings();
    if (failures != 0) {
        std::cerr << failures << " balance shift assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "balance shift passed\n";
    return EXIT_SUCCESS;
}
