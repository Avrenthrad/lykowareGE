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

// Active ragdoll (ADR 0081): motors that pull the bodies toward the animated pose, with a stumble,
// a fall and a recovery. Angles are the worst joint of read_pose() against the animated pose, in
// radians; offsets are how far the root bone's joint is from its animated place, in world metres.
struct ActiveProfile {
    float strength = .5F;          // the motors' pull per tick, in (0, 1]: follow() at weight 1 - strength
    float stumble_angle = .15F;    // past this the character stumbles; motors stay on
    float fall_angle = .8F;        // past this, or past fall_offset, it falls: motors off
    float fall_offset = .5F;
    float recovered_angle = .08F;  // within this and recovered_offset ...
    float recovered_offset = .05F;
    float recovered_seconds = .25F; // ... for this long, it has recovered
    float lie_seconds = 1.F;       // a fallen character lies limp this long
    float rise_seconds = 1.F;      // then the motors come back on over this long
};

struct Desc {
    std::vector<Bone> bones;
    float friction = 0.5F;
    // Absent: no active ragdoll; ActiveRagdoll::create refuses. Ragdoll::create ignores it, so
    // the world is the same with or without it.
    std::optional<ActiveProfile> active;
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
    friend class ActiveRagdoll;

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

enum class ActiveState : std::uint8_t {
    Animated,  // motors on, within stumble_angle
    Stumbling, // motors on, knocked past stumble_angle; back to Animated once recovered
    Fallen,    // motors off for lie_seconds
    Rising,    // motors ramp back on over rise_seconds, then Stumbling until recovered
};

// The stumble and recovery state of one ragdoll (ADR 0081). The caller owns it and calls tick()
// once per simulation tick instead of Ragdoll::follow. It stores no world or ragdoll; the same
// inputs give the same states and weights on every machine.
class ActiveRagdoll {
public:
    // Fails (nullopt, error filled) when the profile is absent (the feature is off), or for a
    // strength outside (0, 1], a value not finite, a negative angle, offset or time, a stumble
    // angle not below the fall angle, or a recovered angle or offset not below the stumble angle
    // or fall offset.
    [[nodiscard]] static std::optional<ActiveRagdoll> create(const std::optional<ActiveProfile>& profile,
                                                             std::string& error);

    // Measures how far the bodies are from `animated`, moves the state, and calls doll.follow()
    // at weight(). False, changing nothing, for seconds not finite and > 0, or when the ragdoll
    // refuses (wrong pose size, destroyed, a body gone).
    bool tick(physics::World& world, Ragdoll& doll, const animation::Pose& animated, float seconds);

    [[nodiscard]] ActiveState state() const { return state_; }
    // The blend weight of the last tick: 1 - strength with motors on, 1 when fallen, between while
    // rising. Pass it to Ragdoll::blended_pose for the drawn pose.
    [[nodiscard]] float weight() const { return weight_; }
    // The last tick's measurement, before its follow().
    [[nodiscard]] float angle() const { return angle_; }
    [[nodiscard]] float offset() const { return offset_; }
    [[nodiscard]] const ActiveProfile& profile() const { return profile_; }

private:
    ActiveRagdoll() = default;

    ActiveProfile profile_;
    ActiveState state_ = ActiveState::Animated;
    float weight_ = 0.F;
    float angle_ = 0.F;
    float offset_ = 0.F;
    float timer_ = 0.F; // seconds in Fallen or Rising, or within the recovered bounds
};

} // namespace lykoware::ragdoll
