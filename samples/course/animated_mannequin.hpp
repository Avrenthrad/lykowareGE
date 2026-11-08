#pragma once

// Original procedural training mannequin (ADR 0052). Sample content, not engine game rules.
#include <lykoware/animation.hpp>
#include <lykoware/mesh.hpp>
#include <lykoware/render.hpp>
#include <lykoware/physics.hpp>
#include "locomotion.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <stdexcept>
#include <utility>
#include <vector>

namespace training_mannequin {

namespace math = lykoware::math;
namespace animation = lykoware::animation;

struct Asset {
    lykoware::MeshData mesh;
    std::vector<std::array<std::uint16_t, 4>> joints;
    std::vector<math::Vec4> weights;
    std::vector<lykoware::render::MeshPart> parts;
    animation::Skeleton skeleton;
    animation::Clip idle;
    animation::Clip walk;
    animation::Clip run, jump, fall, land;
};

inline Asset make()
{
    Asset asset;
    const std::array<math::Vec3, 7> local{{{}, {0.F, 0.95F, 0.F}, {0.F, 0.63F, 0.F},
                                         {-0.36F, 0.45F, 0.F}, {0.36F, 0.45F, 0.F},
                                         {-0.16F, 0.84F, 0.F}, {0.16F, 0.84F, 0.F}}};
    const std::array<const char*, 7> names{{"root", "torso", "head", "left arm", "right arm", "left leg", "right leg"}};
    for (std::size_t i = 0; i < local.size(); ++i) {
        animation::Joint joint{};
        joint.name = names[i];
        if (i != 0) { joint.parent = i >= 2 && i <= 4 ? std::uint16_t{1} : std::uint16_t{0}; }
        joint.rest.translation = local[i];
        asset.skeleton.joints.push_back(joint);
    }
    std::vector<math::Mat4> globals;
    animation::global_transforms(asset.skeleton, animation::rest_pose(asset.skeleton), globals);
    for (std::size_t i = 0; i < globals.size(); ++i) {
        asset.skeleton.joints[i].inverse_bind = math::rigid_inverse(globals[i]);
    }
    struct Box { math::Vec3 centre; math::Vec3 half; std::uint16_t joint; math::Vec3 colour; };
    const Box boxes[] = {
        {{0.F, 1.15F, 0.F}, {0.24F, 0.30F, 0.15F}, 1, {0.2F, 0.42F, 0.8F}},
        {{0.F, 1.58F, 0.F}, {0.18F, 0.18F, 0.17F}, 2, {0.95F, 0.82F, 0.3F}},
        {{-0.36F, 1.10F, 0.F}, {0.10F, 0.30F, 0.10F}, 3, {0.18F, 0.35F, 0.65F}},
        {{0.36F, 1.10F, 0.F}, {0.10F, 0.30F, 0.10F}, 4, {0.18F, 0.35F, 0.65F}},
        {{-0.16F, 0.42F, 0.F}, {0.12F, 0.42F, 0.12F}, 5, {0.12F, 0.22F, 0.42F}},
        {{0.16F, 0.42F, 0.F}, {0.12F, 0.42F, 0.12F}, 6, {0.12F, 0.22F, 0.42F}},
    };
    struct Face { math::Vec3 normal; math::Vec3 u; math::Vec3 v; };
    const Face faces[] = {
        {{1.F,0.F,0.F},{0.F,0.F,-1.F},{0.F,1.F,0.F}}, {{-1.F,0.F,0.F},{0.F,0.F,1.F},{0.F,1.F,0.F}},
        {{0.F,1.F,0.F},{1.F,0.F,0.F},{0.F,0.F,-1.F}}, {{0.F,-1.F,0.F},{1.F,0.F,0.F},{0.F,0.F,1.F}},
        {{0.F,0.F,1.F},{1.F,0.F,0.F},{0.F,1.F,0.F}}, {{0.F,0.F,-1.F},{-1.F,0.F,0.F},{0.F,1.F,0.F}},
    };
    const std::array<math::Vec2, 4> corners{{{-1.F,-1.F},{1.F,-1.F},{1.F,1.F},{-1.F,1.F}}};
    for (const auto& box : boxes) {
        const auto first = static_cast<std::uint32_t>(asset.mesh.indices.size());
        for (const auto& face : faces) {
            const auto base = static_cast<std::uint32_t>(asset.mesh.positions.size());
            for (const auto corner : corners) {
                const auto p = face.normal + face.u * corner.x + face.v * corner.y;
                asset.mesh.positions.push_back(box.centre + math::Vec3{p.x * box.half.x, p.y * box.half.y, p.z * box.half.z});
                asset.mesh.normals.push_back(face.normal);
                asset.mesh.uvs.push_back({(corner.x + 1.F) * 0.5F, (1.F - corner.y) * 0.5F});
                asset.joints.push_back({box.joint, 0, 0, 0});
                asset.weights.push_back({1.F, 0.F, 0.F, 0.F});
            }
            for (const std::uint32_t index : {0U,1U,2U,0U,2U,3U}) { asset.mesh.indices.push_back(base + index); }
        }
        asset.parts.push_back({first, static_cast<std::uint32_t>(asset.mesh.indices.size()) - first, box.colour});
    }
    asset.idle.name = "breathe";
    animation::Channel breathe{};
    breathe.joint = 1;
    breathe.target = animation::Target::Translation;
    breathe.times = {0.F, 1.F, 2.F};
    breathe.values = {{0.F, 0.95F, 0.F, 0.F}, {0.F, 0.97F, 0.F, 0.F}, {0.F, 0.95F, 0.F, 0.F}};
    asset.idle.channels.push_back(breathe);
    asset.walk.name = "walk";
    for (std::uint16_t joint : {std::uint16_t{3}, std::uint16_t{4}, std::uint16_t{5}, std::uint16_t{6}}) {
        animation::Channel swing{};
        swing.joint = joint;
        swing.target = animation::Target::Rotation;
        swing.times = {0.F, 0.25F, 0.5F, 0.75F, 1.F};
        const float sign = joint == 3 || joint == 6 ? 1.F : -1.F;
        for (float angle : {-0.55F, 0.F, 0.55F, 0.F, -0.55F}) {
            const auto q = math::from_axis_angle({1.F, 0.F, 0.F}, angle * sign);
            swing.values.push_back({q.x, q.y, q.z, q.w});
        }
        asset.walk.channels.push_back(swing);
    }
    asset.run = asset.walk;
    asset.run.name = "run";
    for (auto& channel : asset.run.channels) {
        for (auto& value : channel.values) {
            const auto q = math::from_axis_angle({1, 0, 0}, 3.F * std::atan2(value.x, value.w));
            value = {q.x, q.y, q.z, q.w};
        }
    }
    const auto rotation_clip = [](const char* name, float arm, float left, float right) {
        animation::Clip clip; clip.name = name;
        for (const auto& [joint, angle] : std::array<std::pair<std::uint16_t,float>,4>{{
            {std::uint16_t{3},arm},{std::uint16_t{4},arm},{std::uint16_t{5},left},{std::uint16_t{6},right}}}) {
            animation::Channel channel;
            channel.joint = joint; channel.target = animation::Target::Rotation; channel.times = {0.F};
            const auto q = math::from_axis_angle({1,0,0}, angle);
            channel.values = {{q.x,q.y,q.z,q.w}}; clip.channels.push_back(std::move(channel));
        }
        return clip;
    };
    asset.jump = rotation_clip("jump", -.7F, .5F, -.25F);
    asset.fall = rotation_clip("fall", .4F, -.15F, -.15F);
    asset.land.name = "land";
    animation::Channel compression;
    compression.joint = 1; compression.target = animation::Target::Translation;
    compression.times = {0.F, .08F, .18F};
    compression.values = {{0,.95F,0,0},{0,.84F,0,0},{0,.95F,0,0}};
    asset.land.channels.push_back(std::move(compression));
    return asset;
}

struct Playback {
    float idle_seconds = 0.F;
    float walk_seconds = 0.F;
    float walk_weight = 0.F;
    animation::Pose idle;
    animation::Pose walk;
    animation::Pose blended;
    std::vector<math::Mat4> matrices;
    locomotion::Selector selector;
    animation::PoseCrossfade transition;
    animation::Pose target;
    bool ready = false;
    bool used_fallback = false;

    void advance_character(const Asset& asset, float seconds, const lykoware::physics::CharacterState& state)
    {
        const bool grounded = state.ground == lykoware::physics::GroundState::OnGround;
        advance(asset, seconds, grounded ? std::hypot(state.velocity.x,state.velocity.z) : 0.F,
                grounded, state.velocity.y);
    }

    void advance(const Asset& asset, float seconds, float speed, bool grounded = true, float vertical_speed = 0.F)
    {
        seconds = std::isfinite(seconds) ? std::clamp(seconds, 0.F, 0.1F) : 0.F;
        speed = std::isfinite(speed) ? std::max(0.F, speed) : 0.F;
        idle_seconds = std::fmod(idle_seconds + seconds, std::max(0.001F, animation::duration(asset.idle)));
        walk_seconds = std::fmod(walk_seconds + seconds * std::min(4.F, speed / 2.F), std::max(0.001F, animation::duration(asset.walk)));
        const float target_weight = std::clamp(speed / 2.F, 0.F, 1.F);
        walk_weight = target_weight + (walk_weight - target_weight) * std::exp(-8.F * seconds);
        const bool changed = selector.advance(seconds, speed, grounded, vertical_speed);
        idle.resize(asset.skeleton.joints.size());
        for (std::size_t i = 0; i < idle.size(); ++i) { idle[i] = asset.skeleton.joints[i].rest; }
        walk = idle;
        animation::sample(asset.idle, idle_seconds, true, idle);
        animation::sample(asset.walk, walk_seconds, true, walk);
        blended.resize(idle.size());
        target.resize(idle.size());
        const auto state = selector.state();
        used_fallback = false;
        if (state == locomotion::State::Idle || state == locomotion::State::Walk) {
            animation::blend(idle, walk, walk_weight, target);
        } else {
            const animation::Clip* clip = state == locomotion::State::Run ? &asset.run
                : state == locomotion::State::Jump ? &asset.jump
                : state == locomotion::State::Fall ? &asset.fall : &asset.land;
            if (clip->name.empty() && clip->channels.empty()) {
                used_fallback = true;
                clip = state == locomotion::State::Run ? &asset.walk : &asset.idle;
            }
            for (std::size_t i = 0; i < target.size(); ++i) { target[i] = asset.skeleton.joints[i].rest; }
            const bool gait = state == locomotion::State::Run;
            const float time = gait ? walk_seconds / std::max(.001F, animation::duration(asset.walk)) * animation::duration(*clip)
                : selector.seconds();
            animation::sample(*clip, time, gait, target);
        }
        std::string error;
        if (!ready) {
            if (!transition.reset(target, error)) { throw std::invalid_argument(error); }
            ready = true;
        } else if (changed) {
            if (!transition.begin(state == locomotion::State::Land ? .08F : .14F, error)) { throw std::invalid_argument(error); }
        }
        if (!transition.advance(target, changed ? 0.F : seconds, blended, error)) { throw std::invalid_argument(error); }
        animation::skinning_matrices(asset.skeleton, blended, matrices);
    }
};

}
