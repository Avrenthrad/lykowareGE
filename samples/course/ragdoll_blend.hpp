#pragma once

// The ragdoll blend sample (ADR 0080): the training mannequin walks in place on real physics while
// a scripted weight moves it from animation to ragdoll and part of the way back. The sample path
// runs it from frames through FixedStep; the replay path runs the recorded weights headless. Both
// call Scene::tick, one simulation tick at a time, so they agree at any frame rate.

#include "animated_mannequin.hpp"

#include <lykoware/clock.hpp>
#include <lykoware/physics.hpp>
#include <lykoware/ragdoll.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ragdoll_blend {

namespace physics = lykoware::physics;
namespace ragdoll = lykoware::ragdoll;
namespace animation = lykoware::animation;
namespace math = lykoware::math;

inline constexpr std::uint32_t ticks_per_second = 60;

// The mannequin's bones: a pelvis across the hips, then torso, head, arms and legs. The legs start
// below the torso's capsule: bodies that share no joint collide, and an overlap would push them apart.
inline ragdoll::Desc mannequin_bones()
{
    ragdoll::Desc desc;
    desc.bones.push_back({0, {-.16F, .84F, 0.F}, {.16F, .84F, 0.F}, .1F, 12.F, physics::FixedJoint{}, {1, 0, 0}});
    desc.bones.push_back({1, {0.F, .08F, 0.F}, {0.F, .5F, 0.F}, .15F, 25.F, physics::SwingTwistJoint{.4F, -.3F, .3F}, {1, 0, 0}});
    desc.bones.push_back({2, {0.F, .06F, 0.F}, {0.F, .3F, 0.F}, .12F, 5.F, physics::SwingTwistJoint{.6F, -.8F, .8F}, {1, 0, 0}});
    for (std::uint16_t arm : {std::uint16_t{3}, std::uint16_t{4}}) {
        desc.bones.push_back({arm, {0.F, -.05F, 0.F}, {0.F, -.55F, 0.F}, .06F, 3.F, physics::SwingTwistJoint{1.2F, -.5F, .5F}, {1, 0, 0}});
    }
    for (std::uint16_t leg : {std::uint16_t{5}, std::uint16_t{6}}) {
        desc.bones.push_back({leg, {0.F, -.14F, 0.F}, {0.F, -.75F, 0.F}, .08F, 10.F, physics::SwingTwistJoint{.9F, -.3F, .3F}, {1, 0, 0}});
    }
    return desc;
}

// The scripted weight at a tick: animation for a second, to ragdoll over half a second, ragdoll for
// a second, then back to 0.3 over a second. It depends on the tick only, never on frames.
inline float scripted_weight(std::uint64_t tick)
{
    const float t = static_cast<float>(tick) / static_cast<float>(ticks_per_second);
    if (t < 1.F) {
        return 0.F;
    }
    if (t < 1.5F) {
        return (t - 1.F) / .5F;
    }
    if (t < 2.5F) {
        return 1.F;
    }
    if (t < 3.5F) {
        return 1.F - .7F * (t - 2.5F);
    }
    return .3F;
}

class Scene {
public:
    static std::optional<Scene> make(std::string& error)
    {
        Scene scene;
        scene.world_ = physics::World::create({}, error);
        if (!scene.world_) {
            return std::nullopt;
        }
        physics::BodyDesc ground;
        ground.shape = physics::BoxShape{{20.F, 1.F, 20.F}};
        ground.motion = physics::MotionType::Static;
        ground.position = {0.0, -1.0, 0.0};
        if (!scene.world_->create_body(ground, error)) {
            return std::nullopt;
        }
        scene.asset_ = training_mannequin::make();
        scene.pose_ = animation::rest_pose(scene.asset_.skeleton);
        scene.animate(0.F);
        auto doll = ragdoll::Ragdoll::create(*scene.world_, scene.asset_.skeleton, scene.pose_, {{0.0, .1, 0.0}, {}, 1.F},
                                             mannequin_bones(), error);
        if (!doll) {
            return std::nullopt;
        }
        scene.doll_.emplace(std::move(*doll));
        return scene;
    }

    // One simulation tick at `weight`: sample the walk, pull the bodies, step. False, changing
    // nothing, for a weight the ragdoll refuses.
    bool tick(float weight)
    {
        const float dt = 1.F / static_cast<float>(ticks_per_second);
        animate(static_cast<float>(tick_) * dt);
        if (!doll_->follow(*world_, pose_, weight, dt)) {
            return false;
        }
        world_->step(dt);
        weight_ = weight;
        ++tick_;
        return true;
    }

    [[nodiscard]] std::uint64_t hash() const { return world_->state_hash(); }
    [[nodiscard]] std::uint64_t ticks() const { return tick_; }
    // What a renderer would draw now.
    [[nodiscard]] animation::Pose drawn() const
    {
        animation::Pose out;
        doll_->blended_pose(*world_, pose_, weight_, out);
        return out;
    }
    [[nodiscard]] const physics::World& world() const { return *world_; }
    [[nodiscard]] const ragdoll::Ragdoll& doll() const { return *doll_; }

    // The walk at `seconds`, with rigid bones: the mannequin's walk also slides the leg pivots up
    // and down, which no jointed body can follow, so every joint but the root keeps its rest
    // translation (ADR 0080).
    void animate(float seconds)
    {
        animation::sample(asset_.walk, seconds, true, pose_);
        for (std::size_t j = 1; j < pose_.size(); ++j) {
            pose_[j].translation = asset_.skeleton.joints[j].rest.translation;
        }
    }
    [[nodiscard]] const animation::Pose& animated() const { return pose_; }

private:
    Scene() = default;

    std::unique_ptr<physics::World> world_;
    training_mannequin::Asset asset_;
    animation::Pose pose_;
    std::optional<ragdoll::Ragdoll> doll_;
    std::uint64_t tick_ = 0;
    float weight_ = 0.F;
};

struct Run {
    std::uint64_t hash = 0;
    std::vector<float> weights; // the replay record: one weight per tick
};

// The sample path: frames of 1/frame_hz seconds through FixedStep for `seconds`; each tick takes
// the scripted weight and records it.
inline std::optional<Run> run_sample(std::uint32_t frame_hz, double seconds, std::string& error)
{
    auto scene = Scene::make(error);
    lykoware::FixedStepConfig config;
    config.ticks_per_second = ticks_per_second;
    auto clock = lykoware::FixedStep::create(config, error);
    if (!scene || !clock) {
        return std::nullopt;
    }
    Run run;
    const auto frames = static_cast<std::uint64_t>(seconds * frame_hz);
    for (std::uint64_t frame = 0; frame < frames; ++frame) {
        const auto steps = clock->advance(1'000'000'000LL / frame_hz);
        for (std::uint32_t i = 0; i < steps.ticks; ++i) {
            const float weight = scripted_weight(steps.first_tick + i);
            if (!scene->tick(weight)) {
                error = "the ragdoll refused weight " + std::to_string(weight);
                return std::nullopt;
            }
            run.weights.push_back(weight);
        }
    }
    run.hash = scene->hash();
    return run;
}

// The replay path: the recorded weights, one tick each, with no frames and no clock.
inline std::optional<std::uint64_t> replay(const std::vector<float>& weights, std::string& error)
{
    auto scene = Scene::make(error);
    if (!scene) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < weights.size(); ++i) {
        if (!scene->tick(weights[i])) {
            error = "tick " + std::to_string(i) + ": the ragdoll refused weight " + std::to_string(weights[i]);
            return std::nullopt;
        }
    }
    return scene->hash();
}

} // namespace ragdoll_blend
