#include <lykoware/ragdoll.hpp>

#include <algorithm>
#include <cmath>
#include <utility>
#include <variant>

namespace lykoware::ragdoll {
namespace {

using math::DVec3;
using math::Quat;
using math::Vec3;

constexpr double shortest_segment = 1e-4; // metres

DVec3 add(DVec3 a, DVec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
DVec3 sub(DVec3 a, DVec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
DVec3 widen(Vec3 v) { return {v.x, v.y, v.z}; }
Vec3 narrow(DVec3 v) { return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)}; }
DVec3 mul(DVec3 v, double k) { return {v.x * k, v.y * k, v.z * k}; }
double length(DVec3 v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

// Rotates a double vector by a float quaternion, in double.
DVec3 rotate(Quat q, DVec3 v)
{
    const double qx = q.x;
    const double qy = q.y;
    const double qz = q.z;
    const double qw = q.w;
    // t = 2 * cross(q.xyz, v); v' = v + w * t + cross(q.xyz, t)
    const double tx = 2.0 * (qy * v.z - qz * v.y);
    const double ty = 2.0 * (qz * v.x - qx * v.z);
    const double tz = 2.0 * (qx * v.y - qy * v.x);
    return {v.x + qw * tx + (qy * tz - qz * ty), v.y + qw * ty + (qz * tx - qx * tz),
            v.z + qw * tz + (qx * ty - qy * tx)};
}

bool finite(DVec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

// The rotation taking +Y onto `to` (unit).
Quat along(Vec3 to)
{
    const float c = to.y;
    if (c < -1.F + 1e-6F) {
        return math::from_axis_angle({1.F, 0.F, 0.F}, 3.14159265F);
    }
    const Vec3 axis{to.z, 0.F, -to.x}; // cross(+Y, to)
    const float s = math::length(axis);
    if (s < 1e-7F) {
        return {};
    }
    return math::from_axis_angle({axis.x / s, axis.y / s, axis.z / s}, std::atan2(s, c));
}

struct Frame {
    DVec3 position;
    Quat rotation;
};

// A joint's world frame from its parent's (or the placement's) and its local pose.
Frame child_of(const Frame& parent, const animation::JointPose& local, float scale)
{
    return {add(parent.position, rotate(parent.rotation, mul(widen(local.translation), scale))),
            math::normalize(parent.rotation * local.rotation)};
}

} // namespace

std::optional<Ragdoll> Ragdoll::create(physics::World& world, const animation::Skeleton& skeleton,
                                       const animation::Pose& pose, const Placement& placement, const Desc& desc,
                                       std::string& error)
{
    error.clear();
    if (!animation::valid_skeleton(skeleton, error)) {
        return std::nullopt;
    }
    const std::size_t count = skeleton.joints.size();
    if (pose.size() != count) {
        error = "the pose has " + std::to_string(pose.size()) + " joints; the skeleton has " + std::to_string(count);
        return std::nullopt;
    }
    if (!finite(placement.position) || !std::isfinite(placement.scale) || placement.scale <= 0.F
        || !std::isfinite(placement.rotation.x) || !std::isfinite(placement.rotation.y)
        || !std::isfinite(placement.rotation.z) || !std::isfinite(placement.rotation.w)
        || math::length(Vec3{placement.rotation.x, placement.rotation.y, placement.rotation.z}) + std::abs(placement.rotation.w) < 1e-6F) {
        error = "the placement must be finite, with a non-zero rotation and a scale above 0";
        return std::nullopt;
    }
    if (desc.bones.empty()) {
        error = "a ragdoll needs at least one bone";
        return std::nullopt;
    }
    for (std::size_t j = 0; j < count; ++j) {
        const Vec3 scale = pose[j].scale;
        if (std::abs(scale.x - 1.F) > 1e-3F || std::abs(scale.y - 1.F) > 1e-3F || std::abs(scale.z - 1.F) > 1e-3F) {
            error = "joint " + std::to_string(j) + " is scaled; a ragdoll needs joint scales of 1 (scale the placement instead)";
            return std::nullopt;
        }
    }

    Ragdoll made;
    made.placement_ = placement;
    made.placement_.rotation = math::normalize(placement.rotation);
    made.body_of_joint_.assign(count, -1);
    made.parents_.reserve(count);
    for (const auto& joint : skeleton.joints) {
        made.parents_.push_back(joint.parent);
    }

    // Validate every bone before touching the world.
    for (std::size_t b = 0; b < desc.bones.size(); ++b) {
        const Bone& bone = desc.bones[b];
        const std::string which = "bone " + std::to_string(b) + ": ";
        if (bone.joint >= count) {
            error = which + "joint " + std::to_string(bone.joint) + " is not in the skeleton";
            return std::nullopt;
        }
        if (made.body_of_joint_[bone.joint] >= 0) {
            error = which + "joint " + std::to_string(bone.joint) + " already has a bone";
            return std::nullopt;
        }
        made.body_of_joint_[bone.joint] = static_cast<int>(b);
        if (!finite(bone.start) || !finite(bone.end) || !finite(bone.hinge_axis)) {
            error = which + "the segment and hinge axis must be finite";
            return std::nullopt;
        }
        if (!std::isfinite(bone.radius) || bone.radius <= 0.F || !std::isfinite(bone.mass) || bone.mass <= 0.F) {
            error = which + "radius and mass must be finite and above 0";
            return std::nullopt;
        }
        if (const auto* hinge = std::get_if<physics::HingeJoint>(&bone.limit); hinge != nullptr && hinge->min_angle > hinge->max_angle) {
            error = which + "the hinge limit is reversed (min_angle above max_angle)";
            return std::nullopt;
        }
        if (const auto* cone = std::get_if<physics::SwingTwistJoint>(&bone.limit); cone != nullptr && cone->twist_min > cone->twist_max) {
            error = which + "the twist limit is reversed (twist_min above twist_max)";
            return std::nullopt;
        }
        if (std::holds_alternative<physics::HingeJoint>(bone.limit) && math::length(bone.hinge_axis) < 1e-6F) {
            error = which + "a hinge needs a non-zero axis";
            return std::nullopt;
        }
    }
    // The nearest ancestor with a body, per bone.
    std::vector<int> parent_body(desc.bones.size(), -1);
    int roots = 0;
    for (std::size_t b = 0; b < desc.bones.size(); ++b) {
        for (auto up = made.parents_[desc.bones[b].joint]; up; up = made.parents_[*up]) {
            if (made.body_of_joint_[*up] >= 0) {
                parent_body[b] = made.body_of_joint_[*up];
                break;
            }
        }
        roots += parent_body[b] < 0 ? 1 : 0;
    }
    if (roots != 1) {
        error = "exactly one bone must have no ancestor bone; found " + std::to_string(roots);
        return std::nullopt;
    }

    // World frames of every joint at the pose.
    std::vector<Frame> frames(count);
    const Frame origin{placement.position, made.placement_.rotation};
    for (std::size_t j = 0; j < count; ++j) {
        const auto parent = made.parents_[j];
        frames[j] = child_of(parent ? frames[*parent] : origin, pose[j], placement.scale);
    }

    const auto undo = [&] {
        for (const auto body : made.bodies_) {
            world.destroy_body(body);
        }
    };
    for (std::size_t b = 0; b < desc.bones.size(); ++b) {
        const Bone& bone = desc.bones[b];
        const Frame& at = frames[bone.joint];
        const DVec3 a = add(at.position, rotate(at.rotation, mul(widen(bone.start), placement.scale)));
        const DVec3 z = add(at.position, rotate(at.rotation, mul(widen(bone.end), placement.scale)));
        const DVec3 span = sub(z, a);
        const double len = length(span);
        if (len < shortest_segment) {
            undo();
            error = "bone " + std::to_string(b) + ": the segment is shorter than 0.1 mm";
            return std::nullopt;
        }
        const Vec3 direction = narrow(DVec3{span.x / len, span.y / len, span.z / len});
        physics::BodyDesc body;
        body.shape = physics::CapsuleShape{std::max(static_cast<float>(len / 2.0) - bone.radius, 1e-3F), bone.radius};
        body.position = add(a, DVec3{span.x / 2.0, span.y / 2.0, span.z / 2.0});
        body.rotation = along(direction);
        body.mass = bone.mass;
        body.friction = desc.friction;
        const auto id = world.create_body(body, error);
        if (!id) {
            undo();
            error = "bone " + std::to_string(b) + ": " + error;
            return std::nullopt;
        }
        made.bodies_.push_back(*id);
        const Quat inverse = math::conjugate(body.rotation);
        made.links_.push_back({bone.joint, math::normalize(inverse * at.rotation),
                               rotate(inverse, sub(at.position, body.position))});
        if (parent_body[b] >= 0) {
            physics::JointDesc joint;
            joint.parent = made.bodies_[static_cast<std::size_t>(parent_body[b])];
            joint.child = *id;
            joint.kind = bone.limit;
            joint.anchor = at.position;
            joint.axis = std::holds_alternative<physics::HingeJoint>(bone.limit)
                             ? math::rotate(made.placement_.rotation, math::normalize(bone.hinge_axis))
                             : direction;
            const auto held = world.create_joint(joint, error);
            if (!held) {
                undo();
                error = "bone " + std::to_string(b) + ": " + error;
                return std::nullopt;
            }
            made.joints_.push_back(*held);
        }
    }
    return made;
}

bool Ragdoll::read_pose(const physics::World& world, const animation::Pose& animated, animation::Pose& out) const
{
    const std::size_t count = parents_.size();
    if (bodies_.empty() || animated.size() != count) {
        return false;
    }
    std::vector<physics::BodyState> states;
    states.reserve(bodies_.size());
    for (const auto body : bodies_) {
        const auto state = world.state(body);
        if (!state) {
            return false;
        }
        states.push_back(*state);
    }
    const Frame origin{placement_.position, placement_.rotation};
    const double scale = placement_.scale;
    std::vector<Frame> frames(count);
    animation::Pose result = animated;
    for (std::size_t j = 0; j < count; ++j) {
        const auto parent = parents_[j];
        const Frame& above = parent ? frames[*parent] : origin;
        const int body = body_of_joint_[j];
        if (body < 0) {
            frames[j] = child_of(above, animated[j], placement_.scale);
            continue;
        }
        const auto& state = states[static_cast<std::size_t>(body)];
        const Link& link = links_[static_cast<std::size_t>(body)];
        frames[j] = {add(state.position, rotate(state.rotation, link.offset)), math::normalize(state.rotation * link.rotation)};
        const Quat inverse = math::conjugate(above.rotation);
        result[j].rotation = math::normalize(inverse * frames[j].rotation);
        // Bones do not stretch: only the root bone's joint takes its place from its body.
        bool root = true;
        for (auto up = parent; up; up = parents_[*up]) {
            if (body_of_joint_[*up] >= 0) {
                root = false;
                break;
            }
        }
        if (root) {
            const DVec3 local = rotate(inverse, sub(frames[j].position, above.position));
            result[j].translation = narrow(DVec3{local.x / scale, local.y / scale, local.z / scale});
        } else {
            frames[j].position = add(above.position, rotate(above.rotation, mul(widen(animated[j].translation), scale)));
        }
    }
    out = std::move(result);
    return true;
}

void Ragdoll::destroy(physics::World& world)
{
    for (const auto body : bodies_) {
        world.destroy_body(body);
    }
    bodies_.clear();
    joints_.clear();
}

} // namespace lykoware::ragdoll
