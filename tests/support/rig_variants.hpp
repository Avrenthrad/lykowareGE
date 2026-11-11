#pragma once
// Original procedural leg rigs that vary the things real character rigs vary (ADR 0071): size and
// units, thigh/shin proportions, hip width, knees bent at rest, joint frames that point along the
// bone (as Blender exports them), and extra joints (a root above the pelvis, a spine, toes).
// Every rig faces -Z with its ankles at model height 0 and knees bending toward -Z.
#include "foot_placement.hpp"
#include "rig_roles.hpp"

#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace rig_variants {
namespace a = lykoware::animation;
namespace m = lykoware::math;
using V = m::DVec3;

struct Spec {
    std::string name;
    double leg = 1.6;            // hip-to-ankle length along the bones, in rig units
    double thigh_share = .5;     // the thigh's part of the leg
    double hip_half_width = .2;  // in rig units
    double knee_bend_degrees = 0; // knees bent at rest
    bool bone_axes = false;      // joint frames with +Y along the bone
    bool extras = false;         // a root above the pelvis, a spine and toes
    double actor_scale = 1;      // world metres per rig unit (0.01 for a centimetre rig)
};

struct Rig {
    a::Skeleton skeleton;
    rig_roles::Indices roles{};
    double hip_height = 0; // rig units
};

// The rotation taking +Y onto `to` (unit).
inline m::Quat along(V to)
{
    const V y{0, 1, 0};
    const double c = foot_placement::dot(y, to);
    if (c < -1 + 1e-9) {
        return m::from_axis_angle({1, 0, 0}, std::numbers::pi_v<float>);
    }
    const V axis = foot_placement::cross(y, to);
    const double s = foot_placement::norm(axis);
    if (s < 1e-12) {
        return {};
    }
    return m::from_axis_angle({static_cast<float>(axis.x / s), static_cast<float>(axis.y / s), static_cast<float>(axis.z / s)},
                              static_cast<float>(std::atan2(s, c)));
}

inline Rig make(const Spec& spec)
{
    struct Placed {
        std::string name;
        std::optional<std::uint16_t> parent;
        V position;
        m::Quat frame;
    };
    const double half = spec.knee_bend_degrees * std::numbers::pi / 360.0;
    const double thigh = spec.leg * spec.thigh_share;
    const double shin = spec.leg - thigh;
    const double height = thigh * std::cos(half) + shin * std::cos(half);
    const V thigh_dir{0, -std::cos(half), -std::sin(half)};
    const V shin_dir{0, -std::cos(half), std::sin(half)};
    std::vector<Placed> joints;
    const auto add = [&](std::string name, std::optional<std::uint16_t> parent, V position, V toward) {
        const auto frame = spec.bone_axes ? along(foot_placement::scale(toward, 1.0 / foot_placement::norm(toward))) : m::Quat{};
        joints.push_back({std::move(name), parent, position, frame});
        return static_cast<std::uint16_t>(joints.size() - 1);
    };
    std::optional<std::uint16_t> top;
    if (spec.extras) {
        top = add("root", std::nullopt, {0, 0, 0}, {.6, .8, 0}); // a tilted frame above the pelvis
    }
    Rig rig;
    const auto pelvis = add(std::string{rig_roles::names[0]}, top, {0, height, 0}, {0, 1, .3});
    rig.roles[0] = pelvis;
    for (std::size_t side = 0; side < 2; ++side) {
        const double x = side == 0 ? -spec.hip_half_width : spec.hip_half_width;
        const std::size_t leg = side == 0 ? 1 : 4;
        const V hip{x, height, 0};
        const V knee = hip + foot_placement::scale(thigh_dir, thigh);
        const V ankle = knee + foot_placement::scale(shin_dir, shin);
        rig.roles[leg] = add(std::string{rig_roles::names[leg]}, pelvis, hip, thigh_dir);
        rig.roles[leg + 1] = add(std::string{rig_roles::names[leg + 1]}, rig.roles[leg], knee, shin_dir);
        rig.roles[leg + 2] = add(std::string{rig_roles::names[leg + 2]}, rig.roles[leg + 1], ankle, {0, 0, -1});
    }
    if (spec.extras) {
        add("spine", pelvis, {0, height + .3 * spec.leg, 0}, {0, 1, 0});
        for (std::size_t side = 0; side < 2; ++side) {
            const auto ankle = rig.roles[side == 0 ? 3 : 6];
            const V at = joints[ankle].position + V{0, 0, -.15 * spec.leg};
            add(side == 0 ? "toe_l" : "toe_r", ankle, at, {0, 0, -1});
        }
    }
    for (std::size_t i = 0; i < joints.size(); ++i) {
        a::Joint joint;
        joint.name = joints[i].name;
        joint.parent = joints[i].parent;
        if (joints[i].parent) {
            const auto& parent = joints[*joints[i].parent];
            const V local = foot_placement::rotate(m::conjugate(parent.frame), joints[i].position - parent.position);
            joint.rest.translation = {static_cast<float>(local.x), static_cast<float>(local.y), static_cast<float>(local.z)};
            joint.rest.rotation = m::normalize(m::conjugate(parent.frame) * joints[i].frame);
        } else {
            joint.rest.translation = {static_cast<float>(joints[i].position.x), static_cast<float>(joints[i].position.y),
                                      static_cast<float>(joints[i].position.z)};
            joint.rest.rotation = joints[i].frame;
        }
        rig.skeleton.joints.push_back(joint);
    }
    std::vector<m::Mat4> fk;
    a::global_transforms(rig.skeleton, a::rest_pose(rig.skeleton), fk);
    for (std::size_t i = 0; i < fk.size(); ++i) {
        rig.skeleton.joints[i].inverse_bind = m::rigid_inverse(fk[i]);
    }
    rig.hip_height = height;
    return rig;
}

inline std::vector<Spec> all()
{
    return {
        {"baseline", 1.6, .5, .2, 0, false, false, 1},
        {"child", .5, .5, .08, 0, false, false, 1},
        {"tall", 1.1, .5, .14, 0, false, false, 1},
        {"long thighs", .9, .62, .11, 0, false, false, 1},
        {"bent rest", .9, .5, .11, 30, false, false, 1},
        {"bone axes", .9, .5, .11, 0, true, false, 1},
        {"extras, bone axes, bent", .9, .52, .11, 20, true, true, 1},
        {"centimetre units", 90, .5, 11, 10, true, true, .01},
    };
}
} // namespace rig_variants
