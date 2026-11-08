#pragma once
#include "animated_mannequin.hpp"
#include "foot_placement.hpp"
// Original lower-body debug geometry with a seven-joint direct humanoid leg rig.
namespace stance_fixture {
inline training_mannequin::Asset make()
{
    namespace a = lykoware::animation;
    namespace m = lykoware::math;
    training_mannequin::Asset asset;
    const std::array<m::Vec3, 7> local{
        {{0, 1.6F, 0}, {-.2F, 0, 0}, {0, -.8F, 0}, {0, -.8F, 0}, {.2F, 0, 0}, {0, -.8F, 0}, {0, -.8F, 0}}};
    const std::array<std::uint16_t, 7> parents{{0, 0, 1, 2, 0, 4, 5}};
    for (std::size_t i = 0; i < local.size(); ++i) {
        a::Joint joint;
        joint.name = rig_roles::names[i];
        joint.rest.translation = local[i];
        if (i) {
            joint.parent = parents[i];
        }
        asset.skeleton.joints.push_back(joint);
    }
    std::vector<m::Mat4> fk;
    a::global_transforms(asset.skeleton, a::rest_pose(asset.skeleton), fk);
    for (std::size_t i = 0; i < fk.size(); ++i) {
        asset.skeleton.joints[i].inverse_bind = m::rigid_inverse(fk[i]);
    }
    struct Box {
        m::Vec3 centre, half, colour;
        std::uint16_t joint;
    };
    const std::array<Box, 7> boxes{{{{0, 1.6F, 0}, {.3F, .12F, .12F}, {.3F, .6F, .9F}, 0},
                                    {{-.2F, 1.2F, 0}, {.08F, .4F, .08F}, {.2F, .4F, .7F}, 1},
                                    {{-.2F, .4F, 0}, {.08F, .4F, .08F}, {.2F, .4F, .7F}, 2},
                                    {{-.2F, .01F, -.08F}, {.11F, .04F, .2F}, {.95F, .7F, .15F}, 3},
                                    {{.2F, 1.2F, 0}, {.08F, .4F, .08F}, {.2F, .4F, .7F}, 4},
                                    {{.2F, .4F, 0}, {.08F, .4F, .08F}, {.2F, .4F, .7F}, 5},
                                    {{.2F, .01F, -.08F}, {.11F, .04F, .2F}, {.95F, .7F, .15F}, 6}}};
    struct Face {
        m::Vec3 normal, u, v;
    };
    const std::array<Face, 6> faces{{{{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},
                                     {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
                                     {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},
                                     {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
                                     {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},
                                     {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}}}};
    for (const auto& box : boxes) {
        const auto first = static_cast<std::uint32_t>(asset.mesh.indices.size());
        for (const auto& face : faces) {
            const auto base = static_cast<std::uint32_t>(asset.mesh.positions.size());
            for (const auto corner : std::array<m::Vec2, 4>{{{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}}) {
                const auto p = face.normal + face.u * corner.x + face.v * corner.y;
                asset.mesh.positions.push_back(box.centre +
                                               m::Vec3{p.x * box.half.x, p.y * box.half.y, p.z * box.half.z});
                asset.mesh.normals.push_back(face.normal);
                asset.mesh.uvs.push_back({(corner.x + 1) * .5F, (1 - corner.y) * .5F});
                asset.joints.push_back({box.joint, 0, 0, 0});
                asset.weights.push_back({1, 0, 0, 0});
            }
            for (const auto index : std::array<std::uint32_t, 6>{{0, 1, 2, 0, 2, 3}}) {
                asset.mesh.indices.push_back(base + index);
            }
        }
        asset.parts.push_back(
            {first, static_cast<std::uint32_t>(asset.mesh.indices.size()) - first, box.colour});
    }
    return asset;
}
} // namespace stance_fixture
