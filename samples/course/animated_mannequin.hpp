#pragma once

// Original procedural training mannequin (ADR 0052). Sample content, not engine game rules.
#include <lykoware/animation.hpp>
#include <lykoware/animation_graph.hpp>
#include <lykoware/locomotion.hpp>
#include <lykoware/mesh.hpp>
#include <lykoware/render.hpp>
#include <lykoware/physics.hpp>
#include "locomotion.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
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
    std::shared_ptr<const animation::Graph> graph;
    std::vector<animation::Clip> graph_clips;
    // The ground speed (m/s) each locomotion clip was authored for: how fast the stance foot moves
    // backward relative to the body. Stride matching plays a clip at (actual speed / this).
    // make() MEASURES these from the procedural clips (measure_authored_speed). The defaults are
    // for an imported character whose data does not say: replace them from its clip set (ADR 0074,
    // "the clip interface") or its feet will slide at every speed but these.
    float walk_speed = 1.5F;
    float run_speed = 4.F;
    // The most the torso leans from the motor's lean, radians, whatever the motor asks for.
    float lean_limit = 0.35F;
};

// Where a foot is in model space with `pose` applied. The foot is the leg joint's bind position
// dropped to the ground.
inline math::Vec3 foot_in_model(const Asset& asset, const animation::Pose& pose, std::uint16_t leg)
{
    std::vector<math::Mat4> matrices;
    animation::skinning_matrices(asset.skeleton, pose, matrices);
    const auto& bind = asset.skeleton.joints[leg].rest.translation;
    return animation::skin_point(matrices, {leg, 0, 0, 0}, {1.F, 0.F, 0.F, 0.F}, {bind.x, 0.F, bind.z});
}

// The ground speed a locomotion clip was authored for, measured rather than guessed: sample one
// cycle finely, take the samples where a foot is on the ground (stance), and average how fast that
// foot moves backward (+Z; the mannequin faces -Z) relative to the body. Zero for an empty clip or
// one with no stance.
inline float measure_authored_speed(const Asset& asset, const animation::Clip& clip)
{
    const float length = animation::duration(clip);
    if (length <= 0.F) { return 0.F; }
    constexpr std::size_t steps = 960;
    const float dt = length / static_cast<float>(steps);
    animation::Pose pose = animation::rest_pose(asset.skeleton);
    std::array<std::vector<math::Vec3>, 2> feet;
    float lowest = 1e9F;
    for (std::size_t leg = 0; leg < 2; ++leg) {
        for (std::size_t i = 0; i <= steps; ++i) {
            animation::sample(clip, dt * static_cast<float>(i), true, pose);
            feet[leg].push_back(foot_in_model(asset, pose, static_cast<std::uint16_t>(5 + leg)));
            lowest = std::min(lowest, feet[leg].back().y);
        }
    }
    constexpr float on_ground = 0.002F;
    double total = 0.0;
    int counted = 0;
    for (const auto& foot : feet) {
        for (std::size_t i = 0; i < steps; ++i) {
            if (foot[i].y < lowest + on_ground && foot[i + 1].y < lowest + on_ground) {
                total += static_cast<double>(foot[i + 1].z - foot[i].z) / static_cast<double>(dt);
                ++counted;
            }
        }
    }
    return counted > 0 ? static_cast<float>(total / counted) : 0.F;
}

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
    // A gait: each leg is on the ground for half of the cycle, its foot moving backward at a
    // constant speed relative to the body (so the body can move over it without sliding), then
    // swings forward, lifted. Foot z = -leg * sin(angle); the hip drops so a stance foot stays at
    // y = 0. `stride` is how far the foot travels in stance, so it moves at stride / (duration / 2).
    const auto gait_clip = [&](const char* name, float duration, float stride, float lift, float arm) {
        animation::Clip clip;
        clip.name = name;
        constexpr float leg = 0.84F;
        constexpr int per_half = 32;
        const float half = duration * .5F;
        const float speed = stride / half; // stance foot speed, m/s
        const auto foot_z = [&](float t) { // the right leg: stance first, back is +z
            if (t < half) { return -stride * .5F + speed * t; }
            const float u = (t - half) / half; // 0..1 through the swing
            // Cubic Hermite from the back to the front, leaving and arriving at the stance speed so
            // the foot's world speed is continuous at lift-off and touch-down.
            const float h00 = 2.F * u * u * u - 3.F * u * u + 1.F;
            const float h10 = u * u * u - 2.F * u * u + u;
            const float h01 = -2.F * u * u * u + 3.F * u * u;
            const float h11 = u * u * u - u * u;
            return h00 * stride * .5F + h10 * speed * half - h01 * stride * .5F + h11 * speed * half;
        };
        const auto foot_lift = [&](float t) {
            if (t < half) { return 0.F; }
            // Up quickly and down gently (a square root), so a lifted foot is clearly off the ground.
            return lift * std::sqrt(std::sin(3.14159265F * (t - half) / half));
        };
        for (std::uint16_t joint : {std::uint16_t{5}, std::uint16_t{6}}) {
            animation::Channel swing{};
            swing.joint = joint;
            swing.target = animation::Target::Rotation;
            animation::Channel hip{};
            hip.joint = joint;
            hip.target = animation::Target::Translation;
            animation::Channel arm_swing{};
            arm_swing.joint = joint == 5 ? std::uint16_t{3} : std::uint16_t{4};
            arm_swing.target = animation::Target::Rotation;
            const float offset = joint == 5 ? half : 0.F; // the left leg half a cycle behind the right
            for (int i = 0; i <= 2 * per_half; ++i) {
                const float time = duration * static_cast<float>(i) / static_cast<float>(2 * per_half);
                const float t = std::fmod(time + offset, duration);
                const float angle = std::asin(std::clamp(-foot_z(t) / leg, -1.F, 1.F));
                const auto q = math::from_axis_angle({1.F, 0.F, 0.F}, angle);
                swing.times.push_back(time);
                swing.values.push_back({q.x, q.y, q.z, q.w});
                hip.times.push_back(time);
                hip.values.push_back({joint == 5 ? -0.16F : 0.16F, leg * std::cos(angle) + foot_lift(t), 0.F, 0.F});
                // The arm on the same side swings against its leg.
                const auto qa = math::from_axis_angle({1.F, 0.F, 0.F}, -arm * angle);
                arm_swing.times.push_back(time);
                arm_swing.values.push_back({qa.x, qa.y, qa.z, qa.w});
            }
            clip.channels.push_back(std::move(swing));
            clip.channels.push_back(std::move(hip));
            clip.channels.push_back(std::move(arm_swing));
        }
        return clip;
    };
    asset.walk = gait_clip("walk", 1.F, .88F, .06F, .8F);
    asset.run = gait_clip("run", .5F, 1.25F, .10F, 1.F);
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
    asset.walk_speed = measure_authored_speed(asset, asset.walk);
    asset.run_speed = measure_authored_speed(asset, asset.run);
    return asset;
}

struct Playback {
    float idle_seconds = 0.F;
    float walk_seconds = 0.F;
    float walk_weight = 0.F;
    float run_weight = 0.F;
    // The shared walk/run cycle, 0..1 (advance_motor).
    float stride_phase = 0.F;
    bool was_on_ground_gait = true;
    animation::Pose run_pose;
    animation::Pose idle;
    animation::Pose walk;
    animation::Pose blended;
    std::vector<math::Mat4> matrices;
    locomotion::Selector selector;
    animation::PoseCrossfade transition;
    animation::Pose target;
    std::unique_ptr<animation::GraphPlayer> graph_player;
    std::array<bool,2> planted{};
    std::array<std::uint64_t,2> plant_cycles{};
    bool ready = false;
    bool used_fallback = false;
    std::string transition_error;

    // A respawn/new asset starts observation afresh; buffers are rebound on next advance.
    void reset() noexcept
    {
        idle_seconds = 0.F; walk_seconds = 0.F; walk_weight = 0.F; run_weight = 0.F; stride_phase = 0.F; was_on_ground_gait = true;
        selector = {}; ready = false; used_fallback = false; graph_player.reset(); planted={}; ++plant_cycles[0]; ++plant_cycles[1];
    }

    void advance_character(const Asset& asset, float seconds, const lykoware::physics::CharacterState& state)
    {
        const bool grounded = state.ground == lykoware::physics::GroundState::OnGround;
        advance(asset, seconds, grounded ? std::hypot(state.velocity.x,state.velocity.z) : 0.F,
                grounded, state.velocity.y);
    }

    // Animation follows the motor (ADR 0074). Physics decides where the character is; this plays
    // the gait to match what actually happened.
    //  - Stride matching: the walk and run clips share one phase, advanced so the stance foot moves
    //    backward at `actual_speed` (the speed physics achieved, not the wanted one). Per cycle the
    //    clips cover stride(w) metres, so the phase rate is actual_speed / stride(w), kept within
    //    0.5..1.6 of the authored rate; below the walk's slowest rate the walk fades to idle.
    //  - Gait: speed picks the walk/run weight (a continuous function of speed, smoothed, so a gait
    //    change never pops); sprint is the run at a higher rate.
    //  - Pivot: while the motor pivots the phase is held, so the planted foot stays planted.
    //  - Lean: the torso follows the motor's lean, bounded by the asset's limit.
    //  - Air and landing: as `advance`.
    // A character with a graph is driven by the graph at `actual_speed`, with the lean added.
    void advance_motor(const Asset& asset, float seconds, const lykoware::locomotion::State& motor,
                       float actual_speed, bool grounded, float vertical_speed = 0.F)
    {
        seconds = std::isfinite(seconds) ? std::clamp(seconds, 0.F, 0.1F) : 0.F;
        actual_speed = std::isfinite(actual_speed) ? std::max(0.F, actual_speed) : 0.F;
        if (asset.graph) {
            advance(asset, seconds, actual_speed, grounded, vertical_speed);
            if (apply_lean(asset, motor)) { animation::skinning_matrices(asset.skeleton, blended, matrices); }
            return;
        }
        const bool has_run = !asset.run.channels.empty();
        const float walk_length = std::max(.001F, animation::duration(asset.walk));
        const float run_length = has_run ? std::max(.001F, animation::duration(asset.run)) : walk_length;
        const float walk_slowest = .5F * asset.walk_speed;

        // Weights from speed. Idle to walk: full once the slowest walk rate is reached.
        float walk_target = std::clamp((actual_speed - .1F) / std::max(.01F, walk_slowest - .1F), 0.F, 1.F);
        if (motor.legs == lykoware::locomotion::Legs::Stopped && actual_speed < .1F) { walk_target = 0.F; }
        float run_target = has_run
            ? std::clamp((actual_speed - asset.walk_speed) / std::max(.01F, asset.run_speed - asset.walk_speed), 0.F, 1.F)
            : 0.F;
        if (has_run && motor.gait >= lykoware::locomotion::Gait::Run) { run_target = std::max(run_target, .5F); }
        const float follow = 1.F - std::exp(-12.F * seconds);
        walk_weight += (walk_target - walk_weight) * follow;
        run_weight += (run_target - run_weight) * follow;

        // Stride matching on the weights actually in use.
        const float walk_stride = asset.walk_speed * walk_length; // metres per cycle
        const float run_stride = asset.run_speed * run_length;
        const float stride = walk_stride + (run_stride - walk_stride) * run_weight;
        const float authored_rate = 1.F / walk_length + (1.F / run_length - 1.F / walk_length) * run_weight;
        const bool pivoting = motor.legs == lykoware::locomotion::Legs::Pivoting;
        if (grounded && !pivoting && walk_weight > 0.F) {
            const float rate = std::clamp(actual_speed / std::max(.001F, stride), .5F * authored_rate, 1.6F * authored_rate);
            stride_phase = std::fmod(stride_phase + seconds * rate, 1.F);
        }
        idle_seconds = std::fmod(idle_seconds + seconds, std::max(.001F, animation::duration(asset.idle)));

        const bool changed = selector.advance(seconds, actual_speed, grounded, vertical_speed);
        const auto state = selector.state();
        const bool on_ground_gait = state == locomotion::State::Idle || state == locomotion::State::Walk
            || state == locomotion::State::Run;
        idle.resize(asset.skeleton.joints.size());
        for (std::size_t i = 0; i < idle.size(); ++i) { idle[i] = asset.skeleton.joints[i].rest; }
        walk = idle;
        target.resize(idle.size());
        blended.resize(idle.size());
        animation::sample(asset.idle, idle_seconds, true, idle);
        used_fallback = false;
        if (on_ground_gait) {
            animation::sample(asset.walk, stride_phase * walk_length, true, walk);
            if (has_run && run_weight > 0.F) {
                run_pose = idle;
                animation::sample(asset.run, stride_phase * run_length, true, run_pose);
                animation::blend(walk, run_pose, run_weight, target);
                walk = target;
            }
            animation::blend(idle, walk, walk_weight, target);
        } else {
            const animation::Clip* clip = state == locomotion::State::Jump ? &asset.jump
                : state == locomotion::State::Fall ? &asset.fall : &asset.land;
            if (clip->name.empty() && clip->channels.empty()) { used_fallback = true; clip = &asset.idle; }
            for (std::size_t i = 0; i < target.size(); ++i) { target[i] = asset.skeleton.joints[i].rest; }
            animation::sample(*clip, selector.seconds(), false, target);
        }
        // Crossfade only into and out of the air; gaits blend by weight.
        const bool category_changed = changed && !(on_ground_gait && was_on_ground_gait);
        was_on_ground_gait = on_ground_gait;
        if (!ready) {
            if (!transition.reset(target, transition_error)) { throw std::invalid_argument(transition_error); }
            ready = true;
        } else if (category_changed) {
            if (!transition.begin(state == locomotion::State::Land ? .08F : .14F, transition_error)) { throw std::invalid_argument(transition_error); }
        }
        if (!transition.advance(target, category_changed ? 0.F : seconds, blended, transition_error)) { throw std::invalid_argument(transition_error); }
        apply_lean(asset, motor);
        animation::skinning_matrices(asset.skeleton, blended, matrices);
    }

    // Tilts the torso by the motor's lean: forward is positive when speeding up (the top moves toward
    // -Z), side positive to the character's right (+X). Each within the asset's limit. True when the
    // pose changed.
    bool apply_lean(const Asset& asset, const lykoware::locomotion::State& motor)
    {
        if (blended.size() < 2 || asset.skeleton.joints.size() < 2) { return false; }
        const float limit = std::max(0.F, asset.lean_limit);
        const float forward = std::isfinite(motor.lean_forward) ? std::clamp(motor.lean_forward, -limit, limit) : 0.F;
        const float side = std::isfinite(motor.lean_side) ? std::clamp(motor.lean_side, -limit, limit) : 0.F;
        if (forward == 0.F && side == 0.F) { return false; }
        const auto pitch = math::from_axis_angle({1.F, 0.F, 0.F}, -forward);
        const auto roll = math::from_axis_angle({0.F, 0.F, 1.F}, -side);
        blended[1].rotation = math::normalize(blended[1].rotation * (roll * pitch));
        return true;
    }

    void advance(const Asset& asset, float seconds, float speed, bool grounded = true, float vertical_speed = 0.F)
    {
        seconds = std::isfinite(seconds) ? std::clamp(seconds, 0.F, 0.1F) : 0.F;
        speed = std::isfinite(speed) ? std::max(0.F, speed) : 0.F;
        if(asset.graph){
            if(!graph_player){graph_player=animation::GraphPlayer::create(asset.skeleton,asset.graph_clips,*asset.graph,transition_error);if(!graph_player){throw std::invalid_argument(transition_error);}}
            std::array<float,32> parameters{};
            for(std::size_t i=0;i<graph_player->graph().parameters.size();++i){const auto& p=graph_player->graph().parameters[i];
                parameters[i]=p.name=="speed"?speed:p.name=="grounded"?(grounded?1.F:0.F):p.name=="vertical_speed"?vertical_speed:p.initial;}
            const auto previous_state=graph_player->state();
            if(!graph_player->step(seconds,{parameters.data(),graph_player->graph().parameters.size()},transition_error)){throw std::invalid_argument(transition_error);}
            if(graph_player->state()!=previous_state){planted={};++plant_cycles[0];++plant_cycles[1];}
            // Generic events tie by authored index. At a wrap, the sample must apply
            // outgoing end markers before incoming zero markers so the new cycle wins.
            const auto events=graph_player->events();
            for(std::size_t first=0;first<events.size();){
                std::size_t end=first+1;while(end<events.size()&&events[end].seconds==events[first].seconds){++end;}
                for(unsigned zero=0;zero<2;++zero){for(std::size_t i=first;i<end;++i){
                    const auto& event=events[i];const auto& marker=graph_player->graph().states[event.state].events[event.event];
                    if((marker.seconds==0)!=(zero!=0)){continue;}
                    const auto& name=marker.name;
                    if(name=="left_plant"){planted[0]=true;++plant_cycles[0];}else if(name=="left_lift"){planted[0]=false;++plant_cycles[0];}
                    else if(name=="right_plant"){planted[1]=true;++plant_cycles[1];}else if(name=="right_lift"){planted[1]=false;++plant_cycles[1];}
                }}
                first=end;
            }
            const auto pose=graph_player->pose();blended.assign(pose.begin(),pose.end());
            (void)selector.advance(seconds,speed,grounded,vertical_speed);
            animation::skinning_matrices(asset.skeleton,blended,matrices);return;
        }
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
        if (!ready) {
            if (!transition.reset(target, transition_error)) { throw std::invalid_argument(transition_error); }
            ready = true;
        } else if (changed) {
            if (!transition.begin(state == locomotion::State::Land ? .08F : .14F, transition_error)) { throw std::invalid_argument(transition_error); }
        }
        if (!transition.advance(target, changed ? 0.F : seconds, blended, transition_error)) { throw std::invalid_argument(transition_error); }
        animation::skinning_matrices(asset.skeleton, blended, matrices);
    }
};

}
