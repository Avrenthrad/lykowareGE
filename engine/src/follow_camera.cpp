#include <lykoware/follow_camera.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace lykoware {
namespace {

constexpr float pi = std::numbers::pi_v<float>;

math::DVec3 offset(math::DVec3 point, math::Vec3 by)
{
    return {point.x + static_cast<double>(by.x), point.y + static_cast<double>(by.y), point.z + static_cast<double>(by.z)};
}

}

FollowCamera::FollowCamera(const FollowCameraConfig& config)
    : config_{config}
{
    config_.distance = std::max(0.F, config_.distance);
    config_.min_distance = std::clamp(config_.min_distance, 0.F, config_.distance);
    config_.near = std::max(1e-3F, config_.near);
    config_.clearance = std::max(config_.clearance, config_.near);
    if (config_.min_pitch > config_.max_pitch) {
        std::swap(config_.min_pitch, config_.max_pitch);
    }
    config_.min_pitch = std::clamp(config_.min_pitch, -1.5F, 1.5F);
    config_.max_pitch = std::clamp(config_.max_pitch, -1.5F, 1.5F);
    config_.return_speed = std::max(0.F, config_.return_speed);
    config_.transition_speed = std::isfinite(config_.transition_speed) ? std::max(0.F, config_.transition_speed) : 0.F;
    pitch_ = std::clamp(pitch_, config_.min_pitch, config_.max_pitch);
}

void FollowCamera::turn(float yaw_delta, float pitch_delta)
{
    if (!std::isfinite(yaw_delta) || !std::isfinite(pitch_delta)) {
        return;
    }
    yaw_ = std::remainder(yaw_ + yaw_delta, 2.F * pi);
    if (yaw_ >= pi) {
        yaw_ -= 2.F * pi;
    }
    pitch_ = std::clamp(pitch_ + pitch_delta, config_.min_pitch, config_.max_pitch);
}

void FollowCamera::set_distance(float distance)
{
    if (!std::isfinite(distance)) {
        return;
    }
    distance = std::max(0.F, distance);
    if (config_.transition_speed > 0.F && placed_ && distance != config_.distance) {
        transitioning_ = true;
    }
    config_.distance = distance;
    config_.min_distance = std::min(config_.min_distance, config_.distance);
}

math::Vec3 FollowCamera::back() const
{
    // Yaw 0 looks along -Z, so the camera sits on +Z of the pivot; positive pitch raises it.
    return {std::sin(yaw_) * std::cos(pitch_), std::sin(pitch_), std::cos(yaw_) * std::cos(pitch_)};
}

math::Vec3 FollowCamera::forward() const
{
    return {-std::sin(yaw_), 0.F, -std::cos(yaw_)};
}

math::Vec3 FollowCamera::right() const
{
    return {std::cos(yaw_), 0.F, -std::sin(yaw_)};
}

float FollowCamera::allowed_distance(const physics::World& world, math::DVec3 pivot, math::Vec3 back) const
{
    return allowed_distance(world, pivot, back, config_.distance);
}

float FollowCamera::allowed_distance(const physics::World& world, math::DVec3 pivot, math::Vec3 back, float limit) const
{
    // Five rays fan out from the pivot: to the camera's place and to four points `clearance`
    // across the view from it, each reaching past it by the clearance. They all start at the
    // pivot, which is in open space, so a wall beside the character does not count as a hit at
    // the start of a ray.
    const float reach = limit + config_.clearance;
    const math::Vec3 side = math::normalize(math::cross(back, {0.F, 1.F, 0.F}));
    const math::Vec3 lift = math::cross(side, back);
    const std::array<math::Vec3, 5> spreads{math::Vec3{}, side * config_.clearance, side * -config_.clearance,
                                            lift * config_.clearance, lift * -config_.clearance};
    float allowed = limit;
    for (const math::Vec3 spread : spreads) {
        if (const auto hit = world.cast_ray(pivot, back * reach + spread)) {
            allowed = std::min(allowed, hit->fraction * reach - config_.clearance);
        }
    }
    return std::max(allowed, config_.min_distance);
}

render::Camera FollowCamera::update(const physics::World& world, math::DVec3 pivot, float seconds)
{
    const math::Vec3 direction = back();
    const float step = std::isfinite(seconds) && seconds > 0.F ? seconds : 0.F;
    if (transitioning_) {
        // Walls are probed out to wherever the camera is now, so one between the old and new
        // distance still pulls it in at once; the change of distance itself is travelled.
        const float wall = current_ > 0.F || config_.distance > 0.F
            ? std::max(0.F, allowed_distance(world, pivot, direction, std::max(current_, config_.distance)))
            : 0.F;
        const float goal = std::min(config_.distance, wall);
        if (current_ > wall) {
            current_ = wall;
        } else if (goal < current_) {
            current_ = std::max(goal, current_ - config_.transition_speed * step);
        } else {
            current_ = std::min(goal, current_ + config_.transition_speed * step);
        }
        if (current_ == goal) {
            transitioning_ = false;
        }
    } else {
        const float allowed = config_.distance > 0.F ? allowed_distance(world, pivot, direction) : 0.F;
        if (!placed_ || allowed <= current_) {
            current_ = allowed; // pull in at once
            placed_ = true;
        } else if (step > 0.F) {
            current_ = std::min(allowed, current_ + config_.return_speed * step);
        }
    }
    render::Camera camera;
    camera.vertical_fov = config_.vertical_fov;
    camera.near = config_.near;
    if (current_ > 0.F) {
        camera.eye = offset(pivot, direction * current_);
        camera.target = pivot;
    } else {
        // First person: look along the view from the pivot itself.
        camera.eye = pivot;
        camera.target = offset(pivot, direction * -1.F);
    }
    return camera;
}

}
