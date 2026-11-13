// physics.ragdoll (ADR 0079): a ragdoll built from a skeleton, one body per bone with joints and
// limits, and the pose read back from the bodies. Runs on the training mannequin and on every rig
// of animation.rig_acceptance (rig_variants::all()).

#include "animated_mannequin.hpp"
#include "support/rig_variants.hpp"

#include <lykoware/ragdoll.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {
namespace physics = lykoware::physics;
namespace ragdoll = lykoware::ragdoll;
namespace anim = lykoware::animation;
namespace math = lykoware::math;

int failures = 0;
void check(bool ok, const std::string& why)
{
    if (!ok) {
        std::cerr << "FAIL: " << why << '\n';
        ++failures;
    }
}

constexpr float tick = 1.F / 60.F;
// The round trip, made and read back without a step: rotations within this many radians, the
// root's place within this many model units (float arithmetic over a short chain).
constexpr float round_trip_turn = 1e-5F;
constexpr float round_trip_move = 1e-4F;

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

// The angle of the rotation between a and b, from the vector part (acos of the dot product cannot
// resolve angles below about 1e-3 rad in float).
float angle_between(math::Quat a, math::Quat b)
{
    const math::Quat d = math::conjugate(a) * b;
    return 2.F * std::asin(std::min(1.F, math::length(math::Vec3{d.x, d.y, d.z})));
}

struct Error {
    float turn = 0.F;
    float move = 0.F;
};

Error compare(const anim::Pose& a, const anim::Pose& b)
{
    Error worst;
    for (std::size_t j = 0; j < a.size() && j < b.size(); ++j) {
        worst.turn = std::max(worst.turn, angle_between(a[j].rotation, b[j].rotation));
        worst.move = std::max(worst.move, math::length(a[j].translation - b[j].translation));
    }
    return worst;
}

// Mannequin: a pelvis across the hips on the root, then torso, head, arms and legs.
ragdoll::Desc mannequin_desc()
{
    ragdoll::Desc desc;
    desc.bones.push_back({0, {-.16F, .84F, 0.F}, {.16F, .84F, 0.F}, .1F, 12.F, physics::FixedJoint{}, {1, 0, 0}});
    desc.bones.push_back({1, {0.F, .08F, 0.F}, {0.F, .5F, 0.F}, .15F, 25.F, physics::SwingTwistJoint{.4F, -.3F, .3F}, {1, 0, 0}});
    desc.bones.push_back({2, {0.F, .06F, 0.F}, {0.F, .3F, 0.F}, .12F, 5.F, physics::SwingTwistJoint{.6F, -.8F, .8F}, {1, 0, 0}});
    for (std::uint16_t arm : {std::uint16_t{3}, std::uint16_t{4}}) {
        desc.bones.push_back({arm, {0.F, -.05F, 0.F}, {0.F, -.55F, 0.F}, .06F, 3.F, physics::SwingTwistJoint{1.2F, -.5F, .5F}, {1, 0, 0}});
    }
    for (std::uint16_t leg : {std::uint16_t{5}, std::uint16_t{6}}) {
        desc.bones.push_back({leg, {0.F, -.05F, 0.F}, {0.F, -.75F, 0.F}, .08F, 10.F, physics::SwingTwistJoint{.9F, -.3F, .3F}, {1, 0, 0}});
    }
    return desc;
}

// A rig_variants rig: pelvis across the hips, thighs, and shins on knee hinges about model X.
ragdoll::Desc rig_desc(const rig_variants::Rig& rig, const rig_variants::Spec& spec)
{
    const auto& joints = rig.skeleton.joints;
    const auto at = [&](std::size_t role) { return static_cast<std::uint16_t>(rig.roles[role]); };
    const float radius = static_cast<float>(.06 * spec.leg * spec.actor_scale);
    ragdoll::Desc desc;
    desc.bones.push_back({at(0), joints[at(1)].rest.translation, joints[at(4)].rest.translation, radius, 10.F,
                          physics::FixedJoint{}, {1, 0, 0}});
    for (std::size_t leg : {std::size_t{1}, std::size_t{4}}) {
        desc.bones.push_back({at(leg), {}, joints[at(leg + 1)].rest.translation, radius, 8.F,
                              physics::SwingTwistJoint{.9F, -.3F, .3F}, {1, 0, 0}});
        desc.bones.push_back({at(leg + 1), {}, joints[at(leg + 2)].rest.translation, radius * .8F, 4.F,
                              physics::HingeJoint{-2.6F, .05F}, {1, 0, 0}});
    }
    return desc;
}

void test_mannequin()
{
    const auto asset = training_mannequin::make();
    auto pose = anim::rest_pose(asset.skeleton);
    anim::sample(asset.walk, .3F, true, pose);
    const ragdoll::Placement placement{{2.0, .1, 1.0}, math::from_axis_angle({0, 1, 0}, .5F), 1.F};
    auto world = make_world();
    std::string error;
    auto doll = ragdoll::Ragdoll::create(*world, asset.skeleton, pose, placement, mannequin_desc(), error);
    check(doll.has_value(), "the mannequin ragdoll is made: " + error);
    if (!doll) {
        return;
    }
    check(doll->bodies().size() == 7 && doll->joints().size() == 6 && world->body_count() == 7,
          "one body per bone, one joint per non-root bone");

    anim::Pose back;
    check(doll->read_pose(*world, pose, back), "the pose reads back");
    const Error trip = compare(pose, back);
    std::cout << "mannequin round trip: " << trip.turn << " rad, " << trip.move << " units\n";
    check(trip.turn < round_trip_turn && trip.move < round_trip_move, "the mannequin round trip matches");

    // Deliberate break: turn the left arm's body away; the round trip check would catch it.
    const auto arm = *world->state(doll->bodies()[3]);
    world->drive_to_pose(doll->bodies()[3], arm.position, math::normalize(arm.rotation * math::from_axis_angle({1, 0, 0}, .3F)), tick);
    world->step(tick);
    anim::Pose moved;
    doll->read_pose(*world, pose, moved);
    const float arm_turn = angle_between(pose[3].rotation, moved[3].rotation);
    std::cout << "arm turned by its body: " << arm_turn << " rad\n";
    check(arm_turn > round_trip_turn * 100.F, "a moved body moves its joint in the read pose");

    // Dropped on the ground for three seconds: the joints hold and nothing blows up.
    physics::BodyDesc ground;
    ground.shape = physics::BoxShape{{20, 1, 20}};
    ground.motion = physics::MotionType::Static;
    ground.position = {0, -1, 0};
    (void)world->create_body(ground, error);
    for (int i = 0; i < 180; ++i) {
        world->step(tick);
    }
    anim::Pose settled;
    check(doll->read_pose(*world, pose, settled), "the settled pose reads back");
    bool finite = true;
    for (const auto& joint : settled) {
        finite = finite && std::isfinite(joint.rotation.x) && std::isfinite(joint.rotation.w) && std::isfinite(joint.translation.y);
    }
    check(finite, "the settled pose is finite");
    float lowest = 1e9F;
    for (const auto body : doll->bodies()) {
        lowest = std::min(lowest, static_cast<float>(world->state(body)->position.y));
    }
    check(lowest > 0.F && lowest < .5F, "the ragdoll lies on the ground, not through it");

    // A missing bone: without the head's body, the head keeps its animated local pose.
    auto headless = mannequin_desc();
    headless.bones.erase(headless.bones.begin() + 2);
    auto other = make_world();
    auto partial = ragdoll::Ragdoll::create(*other, asset.skeleton, pose, placement, headless, error);
    check(partial.has_value(), "a ragdoll without a head bone is made: " + error);
    if (partial) {
        auto animated = pose;
        animated[2].rotation = math::from_axis_angle({0, 0, 1}, .4F);
        anim::Pose read;
        partial->read_pose(*other, animated, read);
        check(read[2].rotation.x == animated[2].rotation.x && read[2].rotation.z == animated[2].rotation.z
                  && read[2].rotation.w == animated[2].rotation.w,
              "a joint without a body keeps its animated pose");
    }

    // Lifetime: a body removed from the world ends reading; destroy removes the rest.
    world->destroy_body(doll->bodies()[6]);
    check(!doll->read_pose(*world, pose, back), "a ragdoll with a body gone does not read back");
    doll->destroy(*world);
    check(world->body_count() == 1 && world->joint_count() == 0, "destroy removes the bodies and joints");
    check(!doll->read_pose(*world, pose, back), "a destroyed ragdoll does not read back");
}

void test_rigs()
{
    for (const auto& spec : rig_variants::all()) {
        const auto rig = rig_variants::make(spec);
        auto pose = anim::rest_pose(rig.skeleton);
        // Bend one knee and swing one hip so the round trip is not only the rest pose.
        pose[rig.roles[2]].rotation = math::normalize(pose[rig.roles[2]].rotation * math::from_axis_angle({1, 0, 0}, -.4F));
        pose[rig.roles[4]].rotation = math::normalize(pose[rig.roles[4]].rotation * math::from_axis_angle({0, 0, 1}, .2F));
        const ragdoll::Placement placement{{-1.0, .2, 3.0}, math::from_axis_angle({0, 1, 0}, -.7F),
                                           static_cast<float>(spec.actor_scale)};
        auto world = make_world();
        std::string error;
        auto doll = ragdoll::Ragdoll::create(*world, rig.skeleton, pose, placement, rig_desc(rig, spec), error);
        check(doll.has_value(), spec.name + ": the ragdoll is made: " + error);
        if (!doll) {
            continue;
        }
        anim::Pose back;
        check(doll->read_pose(*world, pose, back), spec.name + ": the pose reads back");
        const Error trip = compare(pose, back);
        // The root's place is in model units; a centimetre rig's is 100 times larger.
        const float move_tolerance = round_trip_move / static_cast<float>(spec.actor_scale);
        std::cout << spec.name << " round trip: " << trip.turn << " rad, " << trip.move << " units\n";
        check(trip.turn < round_trip_turn && trip.move < move_tolerance, spec.name + ": the round trip matches");
    }
}

void test_refusals()
{
    const auto asset = training_mannequin::make();
    const auto pose = anim::rest_pose(asset.skeleton);
    auto world = make_world();
    const auto refused = [&](ragdoll::Desc desc, const std::string& why, const anim::Pose& with = {},
                             ragdoll::Placement where = {}) {
        std::string error;
        const auto made = ragdoll::Ragdoll::create(*world, asset.skeleton, with.empty() ? pose : with, where, desc, error);
        check(!made && !error.empty(), why + " is refused");
        check(world->body_count() == 0 && world->joint_count() == 0, why + " leaves the world unchanged");
        return error;
    };
    auto desc = mannequin_desc();
    desc.bones[4].end = desc.bones[4].start;
    refused(desc, "a zero-length bone (made after four others)");
    desc = mannequin_desc();
    desc.bones[1].limit = physics::SwingTwistJoint{.4F, .5F, .2F};
    const auto reversed = refused(desc, "a reversed twist limit");
    check(reversed.find("reversed") != std::string::npos, "the reversed limit is named as such");
    desc = mannequin_desc();
    desc.bones[5].limit = physics::HingeJoint{.5F, .2F};
    refused(desc, "a reversed hinge limit");
    desc = mannequin_desc();
    desc.bones[3].joint = 40;
    refused(desc, "a joint out of range");
    desc = mannequin_desc();
    desc.bones[3].joint = 4;
    refused(desc, "two bones on one joint");
    desc = mannequin_desc();
    desc.bones.erase(desc.bones.begin());
    refused(desc, "two root bones (legs and torso without the pelvis)");
    desc = mannequin_desc();
    desc.bones[2].mass = 0.F;
    refused(desc, "a zero mass");
    desc = mannequin_desc();
    desc.bones[2].radius = std::numeric_limits<float>::quiet_NaN();
    refused(desc, "a non-finite radius");
    refused({}, "no bones");
    refused(mannequin_desc(), "a pose of the wrong size", anim::Pose(3));
    auto scaled = pose;
    scaled[1].scale = {2, 2, 2};
    refused(mannequin_desc(), "a scaled joint", scaled);
    refused(mannequin_desc(), "a placement scale of 0", {}, ragdoll::Placement{{}, {}, 0.F});
}

} // namespace

int main()
{
    test_mannequin();
    test_rigs();
    test_refusals();
    if (failures == 0) {
        std::cout << "physics.ragdoll: all checks passed\n";
    }
    return failures == 0 ? 0 : 1;
}
