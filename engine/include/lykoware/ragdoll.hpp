#pragma once

// Ragdolls (ADR 0079): one physics body per chosen bone of a skeleton, held by the joints of
// ADR 0073 with limits, and a pose read back from the bodies.

#include <lykoware/animation.hpp>
#include <lykoware/math.hpp>
#include <lykoware/physics.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lykoware::ragdoll {

// Where the skeleton's model space sits in the world: model units are scaled by `scale` (world
// metres per model unit, > 0), then rotated, then moved.
struct Placement {
    math::DVec3 position;
    math::Quat rotation;
    float scale = 1.F;
};

// One body: a capsule along a segment of a joint's bone, which moves with that joint.
struct Bone {
    std::uint16_t joint = 0;
    // The segment's ends in the joint's own space, in model units. They must be at least 0.1 mm
    // apart in the world.
    math::Vec3 start;
    math::Vec3 end;
    float radius = 0.05F; // world metres, > 0
    float mass = 1.F;     // kilograms, > 0
    // The joint to the body of the nearest ancestor joint that has one, at this joint's origin;
    // ignored for the root bone. Angles are measured from the pose the ragdoll is made in.
    physics::JointKind limit = physics::SwingTwistJoint{};
    // A hinge's turning axis in model space (any non-zero length). A swing-twist joint twists
    // about the segment instead.
    math::Vec3 hinge_axis{1.F, 0.F, 0.F};
};

struct Desc {
    std::vector<Bone> bones;
    float friction = 0.5F;
};

// A ragdoll's bodies and joints, which live in the world it was made in.
//
// Lifetime: the world owns them. destroy() removes them; destroying the world removes them too,
// and read_pose() then returns false. A Ragdoll object without destroy() leaves its bodies in
// the world.
//
// Missing bones: a joint without a body keeps its animated local pose under its parent, so it
// follows the nearest ancestor that has a body.
//
// Threading: one thread at a time, with the world's own rule.
class Ragdoll {
public:
    // Makes the bodies at `pose`. Fails (nullopt, error filled, the world unchanged) for an
    // invalid skeleton, a pose of the wrong size, a placement that is not finite or has a scale
    // not > 0, no bones, a joint out of range or with two bones, a joint scale other than 1, a
    // segment under 0.1 mm, a radius or mass not finite and > 0, a reversed limit (min above
    // max), not exactly one bone without an ancestor bone, or any refusal from the world.
    [[nodiscard]] static std::optional<Ragdoll> create(physics::World& world, const animation::Skeleton& skeleton,
                                                       const animation::Pose& pose, const Placement& placement,
                                                       const Desc& desc, std::string& error);

    // The pose the bodies hold now: rotations of joints with bodies from their bodies, the root
    // bone's joint also moved; every other joint as in `animated`. False, leaving `out` alone,
    // after destroy(), if any body is gone, or if `animated` has the wrong size.
    bool read_pose(const physics::World& world, const animation::Pose& animated, animation::Pose& out) const;

    // Blending (ADR 0080). `weight` 0 is the animated pose, 1 is the physics pose; the caller owns
    // it and passes it every tick. follow() pulls each body (1 - weight) of the way toward where
    // `animated` puts it, by velocities for the next step(seconds), so contacts and joints still
    // act; at weight 1 it leaves the bodies alone. Call it once per tick before World::step.
    // False, changing nothing, for a weight outside 0..1 or not finite, seconds not finite and
    // > 0, `animated` of the wrong size, after destroy(), or with a body gone.
    bool follow(physics::World& world, const animation::Pose& animated, float weight, float seconds);
    // out = `animated` blended `weight` of the way to the pose the bodies hold (read_pose). Same
    // refusals as follow(), leaving `out` alone.
    bool blended_pose(const physics::World& world, const animation::Pose& animated, float weight,
                      animation::Pose& out) const;
    // Where model space sits from now on, for follow() (a character that walks).
    bool set_placement(const Placement& placement);
    [[nodiscard]] const Placement& placement() const { return placement_; }

    // Removes the bodies (and with them the joints) that are still in the world.
    void destroy(physics::World& world);

    // One body per bone, in the order of Desc::bones.
    [[nodiscard]] const std::vector<physics::BodyId>& bodies() const { return bodies_; }
    [[nodiscard]] const std::vector<physics::JointId>& joints() const { return joints_; }

private:
    struct Link {
        std::uint16_t joint = 0;
        // The joint's frame in its body's frame, in world units.
        math::Quat rotation;
        math::DVec3 offset;
    };

    Ragdoll() = default;

    std::vector<std::optional<std::uint16_t>> parents_; // the skeleton's, copied
    Placement placement_;
    std::vector<physics::BodyId> bodies_;
    std::vector<physics::JointId> joints_;
    std::vector<Link> links_;
    std::vector<int> body_of_joint_; // index into bodies_ per skeleton joint, or -1
};

} // namespace lykoware::ragdoll
