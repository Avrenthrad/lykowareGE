#pragma once

#include <lykoware/math.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Skeletal animation (ADR 0049): skeletons, animation clips, sampling a clip into a pose,
// blending poses, and the skinning matrices that move a mesh's vertices with its joints. Lykoware's
// own code. Clips follow glTF 2.0's animation model (keyed translation, rotation and scale per
// joint; step, linear and cubic-spline interpolation), so the glTF reader can fill them as they
// are.
//
// Conventions are the maths module's (ADR 0024): right-handed, +Y up, metres, radians; matrices
// are column-major and multiply column vectors.
//
// Determinism: sampling and skinning use +, -, *, / and sqrt, plus acos and sin for rotations
// keyed far apart. Those two come from the C library, whose results can differ in the last bit
// between platforms, so poses are for drawing and for gameplay that tolerates that, not for
// simulation state that must replay bit for bit across platforms.
//
// Threading: everything here is plain data and pure functions; separate poses may be sampled on
// separate threads.

namespace lykoware::animation {

// The most joints a skeleton may have.
inline constexpr std::size_t max_joints = 256;

// A joint's place relative to its parent: scale first, then rotate, then move.
struct JointPose {
    math::Vec3 translation;
    math::Quat rotation;
    math::Vec3 scale{1.F, 1.F, 1.F};
};

struct Joint {
    std::string name;
    // The parent joint, which comes earlier in the skeleton; none for a root.
    std::optional<std::uint16_t> parent;
    // The joint's pose when no clip moves it.
    JointPose rest;
    // Takes a vertex from the mesh's bind space into this joint's space (glTF's inverse bind
    // matrix). The identity when the mesh was modelled at the joints' rest places.
    math::Mat4 inverse_bind;
};

// Joints in an order where every parent comes before its children.
struct Skeleton {
    std::vector<Joint> joints;
};

enum class Interpolation {
    Step,        // each key's value until the next key
    Linear,      // straight between keys; rotations along the shorter great arc (slerp)
    CubicSpline, // glTF's cubic Hermite spline, with in- and out-tangents per key
};

enum class Target { Translation, Rotation, Scale };

// One keyed property of one joint.
struct Channel {
    std::uint16_t joint = 0;
    Target target = Target::Translation;
    Interpolation interpolation = Interpolation::Linear;
    // Key times in seconds: at least one, not negative, strictly increasing.
    std::vector<float> times;
    // One value per key for Step and Linear; three for CubicSpline (in-tangent, value,
    // out-tangent, as glTF lays them out). Translation and scale use x, y and z; rotation uses
    // x, y, z and w (a quaternion; normalised when sampled).
    std::vector<math::Vec4> values;
};

struct Clip {
    std::string name;
    std::vector<Channel> channels;
};

// A local pose for every joint of a skeleton, in skeleton order.
using Pose = std::vector<JointPose>;

// True when the skeleton can be posed: 1 to max_joints joints, every parent earlier than its
// child, every number finite, rotations of unit length (within 1e-3) and no zero scale.
// Otherwise false, with the reason in error.
[[nodiscard]] bool valid_skeleton(const Skeleton& skeleton, std::string& error);

// True when the clip can be sampled for the skeleton: every channel's joint exists, its times
// are as Channel describes, it has the right number of finite values, and no joint's target is
// keyed by two channels. Otherwise false, with the reason in error.
[[nodiscard]] bool valid_clip(const Clip& clip, const Skeleton& skeleton, std::string& error);

// The time of the clip's last key, in seconds; 0 for a clip without keys.
[[nodiscard]] float duration(const Clip& clip);

// Every joint at its rest pose.
[[nodiscard]] Pose rest_pose(const Skeleton& skeleton);

// Sets the joints and targets the clip keys to their values at `seconds`; other parts of the pose
// are left as they are, so sampling over the rest pose gives the rest pose where the clip is
// silent. When `loop`, the time wraps round the clip's duration; otherwise it is clamped to it.
// Before the first key a channel holds its first value, after the last its last. A non-finite time
// counts as 0. The clip must be valid for a skeleton with pose.size() joints.
void sample(const Clip& clip, float seconds, bool loop, Pose& pose);

// out = a blended `weight` (clamped to 0..1) of the way to b: translations and scales in a straight
// line, rotations along the shorter arc. a, b and out have the same size; out may be a or b.
void blend(const Pose& a, const Pose& b, float weight, Pose& out);

// Each joint's transform in the model's space: its pose after its parent's. out gets one matrix
// per joint.
void global_transforms(const Skeleton& skeleton, const Pose& pose, std::vector<math::Mat4>& out);

// The matrix for each joint that takes a vertex from bind space to where the posed joint carries
// it: its global transform times its inverse bind matrix. At the rest pose, with inverse bind
// matrices that undo the rest pose, each is the identity.
void skinning_matrices(const Skeleton& skeleton, const Pose& pose, std::vector<math::Mat4>& out);

// A bind-space point moved by up to four joints: the sum of weights[i] times matrices[joints[i]]
// applied to it. Weights are used as given (glTF's sum to 1); a joint index past the end of
// matrices counts as weight 0.
[[nodiscard]] math::Vec3 skin_point(std::span<const math::Mat4> matrices, std::array<std::uint16_t, 4> joints,
                                    math::Vec4 weights, math::Vec3 point);

}
