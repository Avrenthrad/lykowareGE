// animation.rig_acceptance (ADR 0071): every foot feature works across varied rigs, not only the
// stance fixture. Each rig in rig_variants::all() runs, on real Jolt ground:
//   A. standing on flat ground: feet stay where animated;
//   B. standing across a step with pelvis correction: each foot reaches its own ground;
//   C. standing on a slope with normal alignment: each foot's up axis meets the surface normal;
//   D. walking, planted with heading lock, body turned 30 degrees: feet keep their heading;
//   E. walking, one foot planted on a step down with single support: it reaches, the swing foot stays.

#include "support/rig_variants.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

namespace {
namespace a = lykoware::animation;
namespace m = lykoware::math;
namespace p = lykoware::physics;
using V = m::DVec3;
using foot_placement::norm;

int failures = 0;
std::string current;
void check(bool ok, const std::string& why)
{
    if (!ok) {
        std::cerr << "FAIL [" << current << "]: " << why << '\n';
        ++failures;
    }
}

constexpr double clearance = .03;

std::unique_ptr<p::World> ground(double left_drop, m::Quat tilt = {})
{
    std::string error;
    auto world = p::World::create({}, error);
    if (!world) {
        std::cerr << "FATAL: " << error << '\n';
        std::exit(EXIT_FAILURE);
    }
    const auto box = [&](V centre, m::Vec3 half) {
        p::BodyDesc desc;
        desc.motion = p::MotionType::Static;
        desc.shape = p::BoxShape{half};
        desc.position = centre;
        desc.rotation = tilt;
        if (!world->create_body(desc, error)) {
            std::cerr << "FATAL: " << error << '\n';
            std::exit(EXIT_FAILURE);
        }
    };
    if (left_drop == 0) {
        box({0, -.5, 0}, {5, .5F, 5});
    } else {
        box({-2.5, -.5 - left_drop, 0}, {2.5F, .5F, 5});
        box({2.5, -.5, 0}, {2.5F, .5F, 5});
    }
    return world;
}

struct Case {
    rig_variants::Rig rig;
    rig_variants::Spec spec;
    a::Pose rest;
    std::vector<m::Mat4> rest_fk;
    double leg_world = 0;
};

V ankle(const Case& c, const a::Pose& pose, const foot_placement::Placement& actor, std::size_t side)
{
    std::vector<m::Mat4> fk;
    a::global_transforms(c.rig.skeleton, pose, fk);
    return foot_placement::to_world(actor, foot_placement::position(fk[c.rig.roles[side == 0 ? 3 : 6]]));
}
// A model-space direction fixed to a joint at rest, carried by the pose into world space.
V joint_axis(const Case& c, const a::Pose& pose, const foot_placement::Placement& actor, std::size_t joint, V model)
{
    std::vector<m::Mat4> fk;
    a::global_transforms(c.rig.skeleton, pose, fk);
    const V local = foot_placement::local_direction(c.rest_fk[joint], model);
    const V moved = foot_placement::rotate(actor.rotation, foot_placement::direction(fk[joint], local));
    return foot_placement::scale(moved, 1.0 / norm(moved));
}

foot_placement::Placement standing(const Case& c)
{
    foot_placement::Placement actor;
    actor.uniform_scale = c.spec.actor_scale;
    actor.feet = {0, clearance, 0};
    return actor;
}

bool prepare(foot_placement::Controller& controller, const Case& c, lykoware::character::FootSettings settings)
{
    std::string error;
    const bool ok = controller.configure(c.rig.skeleton, c.rig.roles, error, clearance, settings);
    check(ok, "controller prepares: " + error);
    return ok;
}

// A refused update leaves no pose to measure: report it and skip the measurements.
bool posed(bool ran, const a::Pose& out, const Case& c, const std::string& label, const std::string& error)
{
    const bool ok = ran && out.size() == c.rig.skeleton.joints.size();
    check(ok, label + ": the update ran and gave a full pose: " + error);
    return ok;
}

void run_case(const Case& c)
{
    std::string error;
    const double step = std::min(.06 * c.leg_world, .09);
    const auto actor = standing(c);

    // A. Flat ground: feet stay where animated.
    {
        auto world = ground(0);
        foot_placement::Controller controller;
        if (prepare(controller, c, {})) {
            a::Pose out;
            bool ran = true;
            for (int i = 0; i < 30; ++i) {
                ran = controller.update(c.rest, actor, *world, .02F, true, out, error) && ran;
            }
            if (posed(ran, out, c, "A", error)) {
                const double moved = std::max(norm(ankle(c, out, actor, 0) - ankle(c, c.rest, actor, 0)),
                                              norm(ankle(c, out, actor, 1) - ankle(c, c.rest, actor, 1)));
                check(moved < 1e-3 && controller.stats().contacts >= 60,
                      "A: on flat ground the feet stay where animated: " + std::to_string(moved));
            }
        }
    }
    // B. A step under the left foot, with pelvis correction.
    {
        auto world = ground(step);
        lykoware::character::FootSettings settings;
        settings.pelvis = true;
        foot_placement::Controller controller;
        if (prepare(controller, c, settings)) {
            a::Pose out;
            bool ran = true;
            for (int i = 0; i < 150; ++i) {
                ran = controller.update(c.rest, actor, *world, .02F, true, out, error) && ran;
            }
            if (posed(ran, out, c, "B", error)) {
                const double left = std::abs(ankle(c, out, actor, 0).y - (clearance - step));
                const double right = std::abs(ankle(c, out, actor, 1).y - clearance);
                check(left < 2e-3 && right < 2e-3 && controller.pelvis_offset() < 0,
                      "B: across a step each foot reaches its ground: " + std::to_string(left) + ", " + std::to_string(right));
            }
        }
    }
    // C. A 0.2 rad slope with normal alignment.
    {
        const auto tilt = m::from_axis_angle({1, 0, 0}, .2F);
        auto world = ground(0, tilt);
        lykoware::character::FootSettings settings;
        settings.align_normals = true;
        foot_placement::Controller controller;
        if (prepare(controller, c, settings)) {
            a::Pose out;
            bool ran = true;
            for (int i = 0; i < 120; ++i) {
                ran = controller.update(c.rest, actor, *world, .02F, true, out, error) && ran;
            }
            if (posed(ran, out, c, "C", error)) {
                const auto normal = m::rotate(tilt, m::Vec3{0, 1, 0});
                double worst = 0;
                for (const std::size_t side : {std::size_t{0}, std::size_t{1}}) {
                    const V up = joint_axis(c, out, actor, c.rig.roles[side == 0 ? 3 : 6], {0, 1, 0});
                    worst = std::max(worst, norm(up - V{normal.x, normal.y, normal.z}));
                }
                check(worst < 3e-3, "C: on a slope each foot's up axis meets the normal: " + std::to_string(worst));
            }
        }
    }
    // D. Walking, both feet planted with heading lock, the body turned 30 degrees.
    {
        auto world = ground(0);
        lykoware::character::FootSettings settings;
        settings.locking = true;
        settings.heading = true;
        settings.max_plant_distance = 1;
        foot_placement::Controller controller;
        if (prepare(controller, c, settings)) {
            const foot_placement::PlantPhases both{{true, true}};
            a::Pose out;
            // The body rides 2% of a leg lower, as a walk does, so the legs have slack to reach
            // their plants when the turn moves the hips sideways; straight legs would let go.
            auto low = actor;
            low.feet.y -= .02 * c.leg_world;
            auto turned = low;
            bool ran = controller.update(c.rest, low, *world, .01F, true, out, error, both);
            turned.rotation = m::from_axis_angle({0, 1, 0}, 30.F * std::numbers::pi_v<float> / 180.F);
            ran = controller.update(c.rest, turned, *world, .01F, true, out, error, both) && ran;
            if (posed(ran, out, c, "D", error)) {
                double worst = 0;
                for (const std::size_t side : {std::size_t{0}, std::size_t{1}}) {
                    const V forward = joint_axis(c, out, turned, c.rig.roles[side == 0 ? 3 : 6], {0, 0, -1});
                    worst = std::max(worst, std::abs(std::atan2(-forward.x, -forward.z)) * 180.0 / std::numbers::pi);
                }
                check(worst < .1 && controller.stats().headed >= 2 && controller.stats().released == 0,
                      "D: planted feet keep their heading as the body turns: " + std::to_string(worst) + " degrees");
            }
        }
    }
    // E. Walking, the left foot planted on a step down with single support.
    {
        auto world = ground(step);
        lykoware::character::FootSettings settings;
        settings.locking = true;
        settings.pelvis = true;
        settings.single_support = true;
        settings.max_plant_distance = 1;
        foot_placement::Controller controller;
        if (prepare(controller, c, settings)) {
            const foot_placement::PlantPhases left_only{{true, false}};
            a::Pose out;
            bool ran = true;
            double reach = 0;
            double swing = 0;
            for (int i = 0; i < 30 && ran; ++i) {
                ran = controller.update(c.rest, actor, *world, .02F, true, out, error, left_only) &&
                      out.size() == c.rig.skeleton.joints.size();
                if (!ran) {
                    break;
                }
                reach = std::max(reach, std::abs(ankle(c, out, actor, 0).y - (clearance - step)));
                swing = std::max(swing, norm(ankle(c, out, actor, 1) - ankle(c, c.rest, actor, 1)));
            }
            if (posed(ran, out, c, "E", error)) {
                check(controller.stats().released == 0 && reach < 2e-3 && swing < 1e-4,
                      "E: one planted foot reaches a step down, the swing foot stays: " + std::to_string(reach) + ", " +
                          std::to_string(swing));
            }
        }
    }
}
} // namespace

int main()
{
    std::size_t rigs = 0;
    for (const auto& spec : rig_variants::all()) {
        current = spec.name;
        Case c{rig_variants::make(spec), spec, {}, {}, spec.leg * spec.actor_scale};
        c.rest = a::rest_pose(c.rig.skeleton);
        a::global_transforms(c.rig.skeleton, c.rest, c.rest_fk);
        std::string error;
        check(lykoware::character::calibrate_feet(c.rig.skeleton, c.rig.roles, error).has_value(), "the rig calibrates: " + error);
        run_case(c);
        ++rigs;
    }
    if (failures != 0) {
        std::cerr << failures << " rig acceptance assertion(s) failed across " << rigs << " rigs\n";
        return EXIT_FAILURE;
    }
    std::cout << "rig acceptance passed for " << rigs << " rigs\n";
    return EXIT_SUCCESS;
}
