#include <algorithm>
#include <cmath>
#include <limits>
#include <lykoware/foot_plant.hpp>
#include <numbers>
namespace lykoware::animation {
namespace {
using V = math::DVec3;
bool finite(V v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
V cross(V a, V b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
V scale(V v, double n)
{
    return {v.x * n, v.y * n, v.z * n};
}
V rotate(math::Quat q, V p)
{
    const V u{q.x, q.y, q.z};
    return p + scale(cross(u, cross(u, p) + scale(p, q.w)), 2);
}
double length(V p)
{
    return std::hypot(p.x, p.y, p.z);
}
} // namespace
std::optional<FootPlant> FootPlant::create(FootPlantPolicy policy, std::string& error)
{
    if (!std::isfinite(policy.clearance) || policy.clearance < 0 || policy.clearance > .3 ||
        !std::isfinite(policy.max_distance) || policy.max_distance < .05 || policy.max_distance > 1 ||
        !std::isfinite(policy.max_slope_degrees) || policy.max_slope_degrees < 0 ||
        policy.max_slope_degrees > 60) {
        error = "Invalid foot-plant clearance/distance/slope policy";
        return std::nullopt;
    }
    FootPlant plant;
    plant.policy_ = policy;
    error.clear();
    return plant;
}
bool FootPlant::update(const physics::World& world, V foot, bool planted, FootPlantResult& out,
                       std::string& error)
{
    constexpr double ray_limit = static_cast<double>(std::numeric_limits<float>::max()) / 4;
    if (!finite(foot) || std::abs(foot.x) > ray_limit || std::abs(foot.y) > ray_limit ||
        std::abs(foot.z) > ray_limit) {
        error = "Foot-plant position exceeds finite ray range";
        return false;
    }
    auto next = *this;
    FootPlantResult result;
    result.target = foot;
    const bool was_locked = support_.has_value();
    const auto finish = [&] {
        result.released = was_locked && !next.support_;
        out = result;
        *this = next;
        error.clear();
        return true;
    };
    if (!planted) {
        next.reset();
        return finish();
    }
    const double minimum_y = std::cos(policy_.max_slope_degrees * std::numbers::pi / 180);
    if (next.support_) {
        const auto body = world.state(*next.support_);
        if (!body) {
            next.release();
            result.status = FootPlantStatus::SupportLost;
            return finish();
        }
        const auto target = body->position + rotate(body->rotation, next.local_point_);
        const auto normal = math::normalize(math::rotate(body->rotation, next.local_normal_));
        if (!finite(target) || !finite(V{normal.x, normal.y, normal.z})) {
            error = "Foot-plant support transform is nonfinite";
            return false;
        }
        if (normal.y <= 0 || normal.y + 1e-6 < minimum_y) {
            next.release();
            result.status = FootPlantStatus::TooSteep;
            return finish();
        }
        if (length(target - foot) > policy_.max_distance) {
            next.release();
            result.status = FootPlantStatus::TooFar;
            return finish();
        }
        result.target = target;
        result.normal = normal;
        result.support = next.support_;
        result.status = FootPlantStatus::Planted;
        return finish();
    }
    if (!next.armed_) {
        return finish();
    }
    next.armed_ = false;
    const auto hit = world.cast_ray(foot + V{0, .35, 0}, {0, -1, 0});
    if (!hit || std::abs(hit->point.y + policy_.clearance - foot.y) > .3) {
        result.status = FootPlantStatus::MissingSurface;
        return finish();
    }
    if (hit->normal.y <= 0 || hit->normal.y + 1e-6 < minimum_y) {
        result.status = FootPlantStatus::TooSteep;
        return finish();
    }
    const auto target = hit->point + V{0, policy_.clearance, 0};
    if (length(target - foot) > policy_.max_distance) {
        result.status = FootPlantStatus::TooFar;
        return finish();
    }
    const auto body = world.state(hit->body);
    if (!body) {
        result.status = FootPlantStatus::SupportLost;
        return finish();
    }
    next.local_point_ = rotate(math::conjugate(body->rotation), target - body->position);
    next.local_normal_ = math::normalize(math::rotate(math::conjugate(body->rotation), hit->normal));
    if (!finite(next.local_point_)) {
        error = "Foot-plant local support position is nonfinite";
        return false;
    }
    next.support_ = hit->body;
    result.target = target;
    result.normal = hit->normal;
    result.support = next.support_;
    result.status = FootPlantStatus::Planted;
    result.acquired = true;
    return finish();
}
} // namespace lykoware::animation
