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
bool finite(math::Vec3 v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
bool finite(math::Quat q)
{
    return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w);
}
struct Pose {
    V position;
    math::Quat rotation;
};
// A drawn pose fit to use: finite, with a non-zero rotation, which is normalised.
std::optional<Pose> usable(const DrawnBody& drawn)
{
    const auto& q = drawn.rotation;
    const float size = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (!finite(drawn.position) || !finite(q) || !std::isfinite(size) || size < 1e-6F) {
        return std::nullopt;
    }
    return Pose{drawn.position, math::normalize(q)};
}
} // namespace
const DrawnBody* find_drawn(std::span<const DrawnBody> drawn, physics::BodyId body) noexcept
{
    for (const auto& entry : drawn) {
        if (entry.body == body) {
            return &entry;
        }
    }
    return nullptr;
}
std::optional<physics::RayHit> drawn_hit(const physics::RayHit& hit, const physics::BodyState& state,
                                         const DrawnBody& drawn, V from)
{
    const auto pose = usable(drawn);
    if (!pose || !finite(hit.point) || !finite(hit.normal) || !finite(from) || !finite(state.position) ||
        !finite(state.rotation)) {
        return std::nullopt;
    }
    // Carry the hit with the body from its physics pose to its drawn pose.
    const auto inverse = math::conjugate(state.rotation);
    const V point = pose->position + rotate(pose->rotation, rotate(inverse, hit.point - state.position));
    const auto normal = math::normalize(math::rotate(pose->rotation, math::rotate(inverse, hit.normal)));
    if (!finite(point) || !finite(normal)) {
        return std::nullopt;
    }
    auto moved = hit;
    moved.normal = normal;
    moved.point = point;
    // Then straight below `from` on the drawn tangent plane through that point.
    const V n{normal.x, normal.y, normal.z};
    if (n.y > 1e-6) {
        const V offset = from - point;
        const double down = (offset.x * n.x + offset.y * n.y + offset.z * n.z) / n.y;
        moved.point = {from.x, from.y - down, from.z};
    }
    return finite(moved.point) ? std::optional{moved} : std::nullopt;
}
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
                       std::string& error, std::span<const DrawnBody> drawn)
{
    return step(world, foot, std::nullopt, planted, out, error, drawn);
}
bool FootPlant::update(const physics::World& world, V foot, math::Vec3 heading, bool planted, FootPlantResult& out,
                       std::string& error, std::span<const DrawnBody> drawn)
{
    if (!std::isfinite(heading.x) || !std::isfinite(heading.y) || !std::isfinite(heading.z) ||
        !(math::length(heading) > 0.F) || !std::isfinite(math::length(heading))) {
        error = "Foot-plant heading must be a finite non-zero direction";
        return false;
    }
    return step(world, foot, heading, planted, out, error, drawn);
}
bool FootPlant::step(const physics::World& world, V foot, std::optional<math::Vec3> heading, bool planted,
                     FootPlantResult& out, std::string& error, std::span<const DrawnBody> drawn)
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
        // The support as drawn when the caller lists it (ADR 0069), else as simulated.
        Pose support{body->position, body->rotation};
        if (const auto* listed = find_drawn(drawn, *next.support_)) {
            const auto pose = usable(*listed);
            if (!pose) {
                error = "Foot-plant drawn support pose must be finite with a non-zero rotation";
                return false;
            }
            support = *pose;
            result.drawn = true;
        }
        const auto target = support.position + rotate(support.rotation, next.local_point_);
        const auto normal = math::normalize(math::rotate(support.rotation, next.local_normal_));
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
        if (next.local_heading_) {
            const auto held = math::normalize(math::rotate(support.rotation, *next.local_heading_));
            if (!std::isfinite(held.x) || !std::isfinite(held.y) || !std::isfinite(held.z)) {
                error = "Foot-plant held heading is nonfinite";
                return false;
            }
            result.heading = held;
        }
        return finish();
    }
    if (!next.armed_) {
        return finish();
    }
    next.armed_ = false;
    auto hit = world.cast_ray(foot + V{0, .35, 0}, {0, -1, 0});
    // A support listed as drawn elsewhere is probed where it is drawn (ADR 0069).
    std::optional<Pose> drawn_support;
    if (hit) {
        if (const auto* listed = find_drawn(drawn, hit->body)) {
            const auto pose = usable(*listed);
            if (!pose) {
                error = "Foot-plant drawn support pose must be finite with a non-zero rotation";
                return false;
            }
            if (const auto state = world.state(hit->body)) {
                const auto moved = drawn_hit(*hit, *state, *listed, foot);
                if (!moved) {
                    error = "Foot-plant drawn support hit is nonfinite";
                    return false;
                }
                hit = moved;
                drawn_support = pose;
            }
        }
    }
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
    Pose support{body->position, body->rotation};
    if (drawn_support) {
        support = *drawn_support;
        result.drawn = true;
    }
    next.local_point_ = rotate(math::conjugate(support.rotation), target - support.position);
    next.local_normal_ = math::normalize(math::rotate(math::conjugate(support.rotation), hit->normal));
    if (!finite(next.local_point_)) {
        error = "Foot-plant local support position is nonfinite";
        return false;
    }
    next.local_heading_.reset();
    if (heading) {
        // The heading's part in the surface plane; none when it points along the normal.
        const auto n = hit->normal;
        const auto along = math::dot(*heading, n);
        const math::Vec3 flat = *heading - n * along;
        if (math::length(flat) > 1e-3F * math::length(*heading)) {
            const auto world_heading = math::normalize(flat);
            next.local_heading_ = math::normalize(math::rotate(math::conjugate(support.rotation), world_heading));
            result.heading = world_heading;
        }
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
