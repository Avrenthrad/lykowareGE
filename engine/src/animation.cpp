#include <lykoware/animation.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace lykoware::animation {
namespace {

bool finite(math::Vec3 v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool finite(math::Vec4 v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::isfinite(v.w);
}

bool finite(math::Quat q)
{
    return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w);
}

bool finite(const math::Mat4& m)
{
    for (const auto& column : m.m) {
        for (const float value : column) {
            if (!std::isfinite(value)) {
                return false;
            }
        }
    }
    return true;
}

float dot(math::Quat a, math::Quat b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

math::Quat as_quat(math::Vec4 v)
{
    return {v.x, v.y, v.z, v.w};
}

math::Vec3 as_vec3(math::Vec4 v)
{
    return {v.x, v.y, v.z};
}

math::Vec3 lerp3(math::Vec3 a, math::Vec3 b, float t)
{
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

// Normalised linear blend along the shorter way round.
math::Quat nlerp(math::Quat a, math::Quat b, float t)
{
    const float sign = dot(a, b) < 0.F ? -1.F : 1.F;
    return math::normalize(math::Quat{a.x + (sign * b.x - a.x) * t, a.y + (sign * b.y - a.y) * t,
                                      a.z + (sign * b.z - a.z) * t, a.w + (sign * b.w - a.w) * t});
}

// Spherical linear interpolation along the shorter arc, at constant angular speed. Nearly equal
// rotations use nlerp, which is the same there and avoids dividing by a tiny sine.
math::Quat slerp(math::Quat a, math::Quat b, float t)
{
    float cosine = dot(a, b);
    if (cosine < 0.F) {
        b = {-b.x, -b.y, -b.z, -b.w};
        cosine = -cosine;
    }
    if (cosine > 0.9995F) {
        return nlerp(a, b, t);
    }
    const float angle = std::acos(std::min(cosine, 1.F));
    const float sine = std::sin(angle);
    const float wa = std::sin((1.F - t) * angle) / sine;
    const float wb = std::sin(t * angle) / sine;
    return math::normalize(
        math::Quat{a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb});
}

// glTF's cubic Hermite spline between keys k and k + 1, `t` (0..1) of the way across an interval
// of `span` seconds: values[3k + 1] is v_k, values[3k + 2] its out-tangent b_k, values[3k + 3]
// the next key's in-tangent a_(k+1) and values[3k + 4] v_(k+1).
math::Vec4 hermite(const std::vector<math::Vec4>& values, std::size_t k, float t, float span)
{
    const float t2 = t * t;
    const float t3 = t2 * t;
    const float h00 = 2.F * t3 - 3.F * t2 + 1.F;
    const float h10 = (t3 - 2.F * t2 + t) * span;
    const float h01 = -2.F * t3 + 3.F * t2;
    const float h11 = (t3 - t2) * span;
    const math::Vec4 v0 = values[3 * k + 1];
    const math::Vec4 b0 = values[3 * k + 2];
    const math::Vec4 a1 = values[3 * k + 3];
    const math::Vec4 v1 = values[3 * k + 4];
    return {h00 * v0.x + h10 * b0.x + h01 * v1.x + h11 * a1.x, h00 * v0.y + h10 * b0.y + h01 * v1.y + h11 * a1.y,
            h00 * v0.z + h10 * b0.z + h01 * v1.z + h11 * a1.z, h00 * v0.w + h10 * b0.w + h01 * v1.w + h11 * a1.w};
}

// The key's value itself: values[k], or values[3k + 1] for a cubic spline.
math::Vec4 key_value(const Channel& channel, std::size_t k)
{
    return channel.interpolation == Interpolation::CubicSpline ? channel.values[3 * k + 1] : channel.values[k];
}

void apply(const Channel& channel, math::Vec4 value, JointPose& joint)
{
    switch (channel.target) {
    case Target::Translation:
        joint.translation = as_vec3(value);
        break;
    case Target::Rotation:
        joint.rotation = math::normalize(as_quat(value));
        break;
    case Target::Scale:
        joint.scale = as_vec3(value);
        break;
    }
}

void sample_channel(const Channel& channel, float time, JointPose& joint)
{
    const std::vector<float>& times = channel.times;
    if (time <= times.front()) {
        apply(channel, key_value(channel, 0), joint);
        return;
    }
    if (time >= times.back()) {
        apply(channel, key_value(channel, times.size() - 1), joint);
        return;
    }
    // times[k] <= time < times[k + 1]
    const auto next = std::upper_bound(times.begin(), times.end(), time);
    const auto k = static_cast<std::size_t>(next - times.begin()) - 1;
    const float span = times[k + 1] - times[k];
    const float t = (time - times[k]) / span;
    switch (channel.interpolation) {
    case Interpolation::Step:
        apply(channel, channel.values[k], joint);
        break;
    case Interpolation::Linear:
        if (channel.target == Target::Rotation) {
            const math::Quat q = slerp(math::normalize(as_quat(channel.values[k])),
                                       math::normalize(as_quat(channel.values[k + 1])), t);
            joint.rotation = q;
        } else {
            apply(channel, {channel.values[k].x + (channel.values[k + 1].x - channel.values[k].x) * t,
                            channel.values[k].y + (channel.values[k + 1].y - channel.values[k].y) * t,
                            channel.values[k].z + (channel.values[k + 1].z - channel.values[k].z) * t, 0.F},
                  joint);
        }
        break;
    case Interpolation::CubicSpline:
        apply(channel, hermite(channel.values, k, t, span), joint);
        break;
    }
}

const char* target_name(Target target)
{
    switch (target) {
    case Target::Translation:
        return "translation";
    case Target::Rotation:
        return "rotation";
    case Target::Scale:
        return "scale";
    }
    return "?";
}

}

bool valid_skeleton(const Skeleton& skeleton, std::string& error)
{
    error.clear();
    if (skeleton.joints.empty() || skeleton.joints.size() > max_joints) {
        error = "a skeleton needs 1 to " + std::to_string(max_joints) + " joints, not "
            + std::to_string(skeleton.joints.size());
        return false;
    }
    for (std::size_t i = 0; i < skeleton.joints.size(); ++i) {
        const Joint& joint = skeleton.joints[i];
        const std::string where = "joint " + std::to_string(i) + (joint.name.empty() ? "" : " (" + joint.name + ")");
        if (joint.parent && *joint.parent >= i) {
            error = where + ": its parent must come before it";
            return false;
        }
        const JointPose& rest = joint.rest;
        if (!finite(rest.translation) || !finite(rest.rotation) || !finite(rest.scale) || !finite(joint.inverse_bind)) {
            error = where + ": a number is not finite";
            return false;
        }
        if (std::abs(std::sqrt(dot(rest.rotation, rest.rotation)) - 1.F) > 1e-3F) {
            error = where + ": its rotation is not of unit length";
            return false;
        }
        if (rest.scale.x == 0.F || rest.scale.y == 0.F || rest.scale.z == 0.F) {
            error = where + ": a zero scale";
            return false;
        }
    }
    return true;
}

bool valid_clip(const Clip& clip, const Skeleton& skeleton, std::string& error)
{
    error.clear();
    std::set<std::pair<std::uint16_t, Target>> keyed;
    for (std::size_t c = 0; c < clip.channels.size(); ++c) {
        const Channel& channel = clip.channels[c];
        const std::string where = "clip " + (clip.name.empty() ? std::string{"(unnamed)"} : clip.name) + ", channel "
            + std::to_string(c);
        if (channel.joint >= skeleton.joints.size()) {
            error = where + ": joint " + std::to_string(channel.joint) + " is not in the skeleton";
            return false;
        }
        if (!keyed.insert({channel.joint, channel.target}).second) {
            error = where + ": joint " + std::to_string(channel.joint) + "'s " + target_name(channel.target)
                + " is keyed twice";
            return false;
        }
        if (channel.times.empty()) {
            error = where + ": no keys";
            return false;
        }
        for (std::size_t k = 0; k < channel.times.size(); ++k) {
            if (!std::isfinite(channel.times[k]) || channel.times[k] < 0.F
                || (k > 0 && !(channel.times[k] > channel.times[k - 1]))) {
                error = where + ": key times must be finite, not negative and strictly increasing";
                return false;
            }
        }
        const std::size_t per_key = channel.interpolation == Interpolation::CubicSpline ? 3 : 1;
        if (channel.values.size() != channel.times.size() * per_key) {
            error = where + ": " + std::to_string(channel.values.size()) + " values for "
                + std::to_string(channel.times.size()) + " keys; expected " + std::to_string(channel.times.size() * per_key);
            return false;
        }
        for (const math::Vec4 value : channel.values) {
            if (!finite(value)) {
                error = where + ": a value is not finite";
                return false;
            }
        }
    }
    return true;
}

float duration(const Clip& clip)
{
    float last = 0.F;
    for (const Channel& channel : clip.channels) {
        if (!channel.times.empty()) {
            last = std::max(last, channel.times.back());
        }
    }
    return last;
}

Pose rest_pose(const Skeleton& skeleton)
{
    Pose pose;
    pose.reserve(skeleton.joints.size());
    for (const Joint& joint : skeleton.joints) {
        pose.push_back(joint.rest);
    }
    return pose;
}

void sample(const Clip& clip, float seconds, bool loop, Pose& pose)
{
    float time = std::isfinite(seconds) ? seconds : 0.F;
    const float length = duration(clip);
    if (loop && length > 0.F) {
        time = std::fmod(time, length);
        if (time < 0.F) {
            time += length;
        }
    } else {
        time = std::clamp(time, 0.F, length);
    }
    for (const Channel& channel : clip.channels) {
        if (channel.joint < pose.size() && !channel.times.empty()) {
            sample_channel(channel, time, pose[channel.joint]);
        }
    }
}

void blend(const Pose& a, const Pose& b, float weight, Pose& out)
{
    const float t = std::isfinite(weight) ? std::clamp(weight, 0.F, 1.F) : 0.F;
    const std::size_t count = std::min({a.size(), b.size(), out.size()});
    for (std::size_t i = 0; i < count; ++i) {
        const JointPose from = a[i];
        const JointPose to = b[i];
        out[i].translation = lerp3(from.translation, to.translation, t);
        out[i].rotation = nlerp(from.rotation, to.rotation, t);
        out[i].scale = lerp3(from.scale, to.scale, t);
    }
}

void global_transforms(const Skeleton& skeleton, const Pose& pose, std::vector<math::Mat4>& out)
{
    const std::size_t count = std::min(skeleton.joints.size(), pose.size());
    out.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        const JointPose& local = pose[i];
        const math::Mat4 own = math::trs(local.translation, local.rotation, local.scale);
        const std::optional<std::uint16_t> parent = skeleton.joints[i].parent;
        out[i] = parent && *parent < i ? out[*parent] * own : own;
    }
}

void skinning_matrices(const Skeleton& skeleton, const Pose& pose, std::vector<math::Mat4>& out)
{
    global_transforms(skeleton, pose, out);
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = out[i] * skeleton.joints[i].inverse_bind;
    }
}

math::Vec3 skin_point(std::span<const math::Mat4> matrices, std::array<std::uint16_t, 4> joints, math::Vec4 weights,
                      math::Vec3 point)
{
    const std::array<float, 4> w{weights.x, weights.y, weights.z, weights.w};
    math::Vec3 sum;
    for (std::size_t i = 0; i < 4; ++i) {
        if (w[i] == 0.F || joints[i] >= matrices.size()) {
            continue;
        }
        const math::Vec3 moved = math::transform_point(matrices[joints[i]], point);
        sum = {sum.x + moved.x * w[i], sum.y + moved.y * w[i], sum.z + moved.z * w[i]};
    }
    return sum;
}

}

namespace lykoware::animation {
namespace {
bool valid_transition_pose(std::span<const JointPose> pose)
{
    if (pose.empty() || pose.size() > max_joints) { return false; }
    for (const auto& joint : pose) {
        if (!finite(joint.translation) || !finite(joint.scale) || !finite(joint.rotation)
            || joint.scale.x == 0.F || joint.scale.y == 0.F || joint.scale.z == 0.F
            || std::abs(std::sqrt(dot(joint.rotation, joint.rotation)) - 1.F) > 1e-3F) { return false; }
    }
    return true;
}
}
bool PoseCrossfade::reset(std::span<const JointPose> initial, std::string& error)
{
    if (!valid_transition_pose(initial)) { error = "Crossfade requires a complete finite pose"; return false; }
    Pose visible(initial.begin(), initial.end());
    Pose source = visible, candidate = visible;
    visible_ = std::move(visible); source_ = std::move(source); candidate_ = std::move(candidate);
    elapsed_ = 0.0; duration_ = 0.0; error.clear(); return true;
}
bool PoseCrossfade::begin(float seconds, std::string& error)
{
    if (visible_.empty() || !std::isfinite(seconds) || seconds < 0.F || seconds > 3600.F) {
        error = "Crossfade needs an initial pose and duration in 0..3600 seconds"; return false;
    }
    std::copy(visible_.begin(), visible_.end(), source_.begin());
    elapsed_ = 0.0; duration_ = seconds; error.clear(); return true;
}
bool PoseCrossfade::advance(std::span<const JointPose> target, float seconds,
    std::span<JointPose> out, std::string& error)
{
    if (target.size() != visible_.size() || out.size() != visible_.size()
        || !std::isfinite(seconds) || seconds < 0.F || !valid_transition_pose(target)) {
        error = "Crossfade requires matching finite poses and nonnegative time"; return false;
    }
    const double next = std::min(duration_, elapsed_ + seconds);
    const float weight = duration_ > 0.0 ? static_cast<float>(next / duration_) : 1.F;
    for (std::size_t i = 0; i < target.size(); ++i) {
        if (weight == 0.F) { candidate_[i] = source_[i]; }
        else if (weight == 1.F) { candidate_[i] = target[i]; }
        else {
            const auto mix = [weight](math::Vec3 a, math::Vec3 b) {
                return math::Vec3{std::lerp(a.x,b.x,weight),std::lerp(a.y,b.y,weight),std::lerp(a.z,b.z,weight)};
            };
            candidate_[i].translation = mix(source_[i].translation, target[i].translation);
            candidate_[i].rotation = slerp(source_[i].rotation, target[i].rotation, weight);
            candidate_[i].scale = mix(source_[i].scale, target[i].scale);
        }
    }
    if (!valid_transition_pose(candidate_)) { error = "Crossfade produces invalid intermediate scales or values"; return false; }
    std::copy(candidate_.begin(), candidate_.end(), visible_.begin());
    std::copy(candidate_.begin(), candidate_.end(), out.begin());
    elapsed_ = next; error.clear(); return true;
}
float PoseCrossfade::progress() const noexcept
{
    return duration_ > 0.0 ? static_cast<float>(elapsed_ / duration_) : 1.F;
}
void PoseCrossfade::swap(PoseCrossfade& other) noexcept
{
    visible_.swap(other.visible_); source_.swap(other.source_); candidate_.swap(other.candidate_);
    std::swap(elapsed_,other.elapsed_); std::swap(duration_,other.duration_);
}
PoseCrossfade::PoseCrossfade(PoseCrossfade&& other) noexcept
    : visible_{std::move(other.visible_)}, source_{std::move(other.source_)}, candidate_{std::move(other.candidate_)},
      elapsed_{std::exchange(other.elapsed_,0.0)}, duration_{std::exchange(other.duration_,0.0)}
{
    other.visible_.clear(); other.source_.clear(); other.candidate_.clear();
}
}
