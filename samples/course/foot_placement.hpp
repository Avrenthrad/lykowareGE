#pragma once
#include "rig_roles.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <lykoware/foot_plant.hpp>
#include <lykoware/ik.hpp>
#include <lykoware/physics.hpp>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace foot_placement {
namespace math = lykoware::math;
namespace animation = lykoware::animation;
using V = math::DVec3;
inline bool finite(V p)
{
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}
inline double norm(V p)
{
    return std::hypot(p.x, p.y, p.z);
}
inline V scale(V p, double s)
{
    return {p.x * s, p.y * s, p.z * s};
}
inline V cross(V a, V b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline V rotate(math::Quat q, V p)
{
    const V u{q.x, q.y, q.z};
    return p + scale(cross(u, cross(u, p) + scale(p, q.w)), 2.0);
}
struct Placement {
    V feet{};
    math::Quat rotation{};
    double uniform_scale = 1.0;
    V offset{};
};
inline bool valid(const Placement& p)
{
    const auto q = p.rotation;
    const double n = static_cast<double>(q.x) * q.x + static_cast<double>(q.y) * q.y +
                     static_cast<double>(q.z) * q.z + static_cast<double>(q.w) * q.w;
    return finite(p.feet) && finite(p.offset) && std::isfinite(p.uniform_scale) && p.uniform_scale > 0.0 &&
           std::isfinite(1.0 / p.uniform_scale) && std::isfinite(n) && std::abs(n - 1.0) < 1e-5;
}
inline V to_world(const Placement& p, V model)
{
    return p.feet + rotate(p.rotation, scale(model, p.uniform_scale) + p.offset);
}
inline V to_model(const Placement& p, V world)
{
    return scale(rotate(math::conjugate(p.rotation), world - p.feet) - p.offset, 1.0 / p.uniform_scale);
}
inline V position(const math::Mat4& m)
{
    return {m.m[3][0], m.m[3][1], m.m[3][2]};
}
inline double dot(V a, V b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
// A joint-local direction carried into model space by a joint's (unit-scale) global transform.
inline V direction(const math::Mat4& m, V local)
{
    return {m.m[0][0] * local.x + m.m[1][0] * local.y + m.m[2][0] * local.z,
            m.m[0][1] * local.x + m.m[1][1] * local.y + m.m[2][1] * local.z,
            m.m[0][2] * local.x + m.m[1][2] * local.y + m.m[2][2] * local.z};
}
// The joint-local direction that `model` (a model-space direction) has at a joint's rest transform.
inline V local_direction(const math::Mat4& rest, V model)
{
    return {rest.m[0][0] * model.x + rest.m[0][1] * model.y + rest.m[0][2] * model.z,
            rest.m[1][0] * model.x + rest.m[1][1] * model.y + rest.m[1][2] * model.z,
            rest.m[2][0] * model.x + rest.m[2][1] * model.y + rest.m[2][2] * model.z};
}
struct PlantPhases {
    std::array<bool, 2> planted;
    std::array<std::uint64_t, 2> cycles;
    PlantPhases(std::array<bool, 2> states, std::array<std::uint64_t, 2> epochs = {})
        : planted{states}, cycles{epochs}
    {
    }
};
struct Stats {
    std::uint64_t frames = 0, contacts = 0, misses = 0, clamped = 0, resets = 0, oriented = 0,
                  slope_rejected = 0, pelvis_frames = 0, planted = 0, released = 0, headed = 0, heading_clamped = 0;
};
class Controller {
public:
    bool configure(const animation::Skeleton& skeleton, const rig_roles::Indices& roles, std::string& error,
                   double clearance = 0.03, lykoware::character::FootSettings settings = {})
    {
        if (!std::isfinite(clearance) || clearance < 0.0 || clearance > 0.3) {
            error = "Foot clearance must be 0..0.3 metres";
            return false;
        }
        if (!lykoware::character::valid_foot_settings(settings, error)) {
            return false;
        }
        const auto calibrated = lykoware::character::calibrate_feet(skeleton, roles, error);
        if (!calibrated) {
            return false;
        }
        auto rest = animation::rest_pose(skeleton);
        std::vector<math::Mat4> fk;
        animation::global_transforms(skeleton, rest, fk);
        std::array<std::optional<animation::PreparedTwoBone>, 2> prepared{
            {calibrated->legs[0], calibrated->legs[1]}};
        std::array<std::optional<animation::PreparedJointAxis>, 2> axes{};
        // Heading lock (ADR 0068): the foot's model -Z, a horizontal reference at right angles to its
        // up axis (model +Y), is what a planted foot keeps pointing along its held heading.
        std::array<V, 2> local_reference{};
        double shorter_leg = std::numeric_limits<double>::max();
        for (std::size_t side = 0; side < 2; ++side) {
            const std::size_t leg = side == 0 ? 1 : 4;
            if (settings.align_normals) {
                axes[side] =
                    animation::PreparedJointAxis::prepare(skeleton, roles[leg + 2], {0, 1, 0}, error);
                if (!axes[side]) {
                    return false;
                }
            }
            local_reference[side] = local_direction(fk[roles[leg + 2]], {0, 0, -1});
            shorter_leg =
                std::min(shorter_leg, norm(position(fk[roles[leg + 1]]) - position(fk[roles[leg]])) +
                                          norm(position(fk[roles[leg + 2]]) - position(fk[roles[leg + 1]])));
        }
        std::array<std::optional<animation::FootPlant>, 2> plants{};
        if (settings.locking) {
            for (auto& plant : plants) {
                plant = animation::FootPlant::create(
                    {clearance, settings.max_plant_distance, settings.max_slope_degrees}, error);
                if (!plant) {
                    return false;
                }
            }
        }
        animation::Skeleton copy = skeleton;
        animation::Pose candidate(rest.size());
        skeleton_ = std::move(copy);
        prepared_ = std::move(prepared);
        candidate_ = std::move(candidate);
        fk_ = std::move(fk);
        roles_ = roles;
        clearance_ = clearance;
        settings_ = settings;
        axes_ = std::move(axes);
        local_reference_ = local_reference;
        plants_ = std::move(plants);
        shorter_leg_ = shorter_leg;
        ready_ = true;
        reset();
        stats_ = {};
        return true;
    }
    void reset()
    {
        for (auto& plant : plants_) {
            if (plant) {
                plant->reset();
            }
        }
        plant_cycles_ = {};
        correction_ = {};
        normals_ = {V{0, 1, 0}, V{0, 1, 0}};
        pelvis_drop_ = 0;
        previous_ = false;
        ++stats_.resets;
    }
    double pelvis_offset() const
    {
        return pelvis_drop_;
    }
    const Stats& stats() const
    {
        return stats_;
    }
    // Transactional pose/contact state; caller can preserve base animation on false.
    bool update(const animation::Pose& base, const Placement& actor, const lykoware::physics::World& world,
                float seconds, bool active, animation::Pose& out, std::string& error,
                std::optional<PlantPhases> planting = std::nullopt)
    {
        if (!ready_ || !valid(actor)) {
            error = "Invalid foot IK configuration or placement";
            return false;
        }
        if (!prepared_[0]->valid_pose(base, error)) {
            return false;
        }
        candidate_ = base;
        auto plants = plants_;
        auto cycles = plant_cycles_;
        const bool walking = settings_.locking && planting.has_value();
        auto next = correction_;
        auto normals = normals_;
        double pelvis = pelvis_drop_;
        Stats stats = stats_;
        ++stats.frames;
        const bool jump = previous_ && norm(actor.feet - previous_feet_) > 1.0;
        const auto commit = [&] {
            out = candidate_;
            correction_ = next;
            plants_ = plants;
            plant_cycles_ = cycles;
            normals_ = normals;
            pelvis_drop_ = pelvis;
            previous_feet_ = actor.feet;
            previous_ = true;
            stats_ = stats;
            error.clear();
            return true;
        };
        if (!active || !std::isfinite(seconds) || seconds < 0.F || seconds > 0.25F || jump) {
            for (auto& plant : plants) {
                if (plant) {
                    plant->reset();
                }
            }
            cycles = {};
            next = {};
            normals = {V{0, 1, 0}, V{0, 1, 0}};
            pelvis = 0;
            ++stats.resets;
            return commit();
        }
        animation::global_transforms(skeleton_, base, fk_);
        std::array<V, 2> targets{};
        std::array<double, 2> desired{};
        std::array<bool, 2> contact{};
        std::array<std::optional<V>, 2> headings{};
        std::array<V, 2> support_normals{};
        const double alpha = 1.0 - std::exp(-12.0 * seconds);
        const double maximum_angle = settings_.max_slope_degrees * std::numbers::pi / 180.0;
        for (std::size_t side = 0; side < 2; ++side) {
            const std::size_t leg = side == 0 ? 1 : 4;
            const V foot = to_world(actor, position(fk_[roles_[leg + 2]]));
            constexpr double ray_limit = static_cast<double>(std::numeric_limits<float>::max()) / 4.0;
            if (!finite(foot) || std::abs(foot.x) > ray_limit || std::abs(foot.y) > ray_limit ||
                std::abs(foot.z) > ray_limit) {
                error = "World foot exceeds finite physics ray range";
                return false;
            }
            V target_normal{0, 1, 0};
            if (walking) {
                if (planting->cycles[side] != cycles[side]) {
                    if (plants[side]->locked()) {
                        ++stats.released;
                    }
                    plants[side]->reset();
                    cycles[side] = planting->cycles[side];
                }
                animation::FootPlantResult result;
                if (settings_.heading) {
                    const V model = direction(fk_[roles_[leg + 2]], local_reference_[side]);
                    const V heading = rotate(actor.rotation, model);
                    if (!plants[side]->update(world, foot,
                                              math::Vec3{static_cast<float>(heading.x), static_cast<float>(heading.y),
                                                         static_cast<float>(heading.z)},
                                              planting->planted[side], result, error)) {
                        return false;
                    }
                } else if (!plants[side]->update(world, foot, planting->planted[side], result, error)) {
                    return false;
                }
                if (result.acquired) {
                    ++stats.planted;
                }
                if (result.released) {
                    ++stats.released;
                }
                if (result.status == animation::FootPlantStatus::TooSteep) {
                    ++stats.slope_rejected;
                }
                if (!result.locked()) {
                    next[side] = 0;
                    normals[side] = {0, 1, 0};
                    if (planting->planted[side]) {
                        ++stats.misses;
                    }
                    continue;
                }
                targets[side] = result.target;
                target_normal = {result.normal.x, result.normal.y, result.normal.z};
                if (result.heading) {
                    headings[side] = V{result.heading->x, result.heading->y, result.heading->z};
                    support_normals[side] = V{result.normal.x, result.normal.y, result.normal.z};
                }
                desired[side] = result.target.y - foot.y;
                next[side] = desired[side];
                contact[side] = true;
            } else {
                if (plants[side]) {
                    animation::FootPlantResult released;
                    if (!plants[side]->update(world, foot, false, released, error)) {
                        return false;
                    }
                    if (released.released) {
                        ++stats.released;
                    }
                }
                const auto hit = world.cast_ray(foot + V{0, .35, 0}, {0, -1, 0});
                if (!hit || !finite(hit->point) || std::abs(hit->point.y + clearance_ - foot.y) > .3) {
                    next[side] = 0;
                    normals[side] = {0, 1, 0};
                    ++stats.misses;
                    continue;
                }
                if ((settings_.align_normals || settings_.pelvis || settings_.locking) &&
                    (hit->normal.y <= 0 || hit->normal.y + 1e-6 < std::cos(maximum_angle))) {
                    next[side] = 0;
                    normals[side] = {0, 1, 0};
                    ++stats.slope_rejected;
                    ++stats.misses;
                    continue;
                }
                desired[side] = hit->point.y + clearance_ - foot.y;
                next[side] += alpha * (desired[side] - next[side]);
                contact[side] = true;
                targets[side] = foot + V{0, next[side], 0};
                target_normal = {hit->normal.x, hit->normal.y, hit->normal.z};
            }
            if (settings_.align_normals) {
                const V blended = normals[side] + scale(target_normal - normals[side], alpha);
                normals[side] = scale(blended, 1.0 / norm(blended));
            }
        }
        Stats probes = stats;
        // At most two locks can be released for reach. Recompute the whole bilateral
        // pose/body correction after a release rather than committing half a solve.
        for (unsigned attempt = 0; attempt < 3; ++attempt) {
            candidate_ = base;
            stats = probes;
            pelvis = pelvis_drop_;

            if (settings_.pelvis && contact[0] && contact[1]) {
                const double bound =
                    std::min(settings_.max_pelvis_drop, shorter_leg_ * actor.uniform_scale * .25);
                const double wanted = std::clamp(std::min(desired[0], desired[1]), -bound, 0.0);
                pelvis = std::clamp(pelvis + alpha * (wanted - pelvis), -bound, 0.0);
                const auto model_delta = scale(rotate(math::conjugate(actor.rotation), V{0, pelvis, 0}),
                                               1.0 / actor.uniform_scale);
                math::Quat parent{};
                if (skeleton_.joints[roles_[0]].parent) {
                    for (std::size_t i = 0; i < base.size(); ++i) {
                        const auto p = skeleton_.joints[i].parent;
                        rotations_[i] =
                            math::normalize(p ? rotations_[*p] * base[i].rotation : base[i].rotation);
                    }
                    parent = rotations_[*skeleton_.joints[roles_[0]].parent];
                }
                const V local_delta = rotate(math::conjugate(parent), model_delta);
                auto& translation = candidate_[roles_[0]].translation;
                translation = translation + math::Vec3{static_cast<float>(local_delta.x),
                                                       static_cast<float>(local_delta.y),
                                                       static_cast<float>(local_delta.z)};
                if (pelvis < 0) {
                    ++stats.pelvis_frames;
                }
            } else {
                pelvis = 0;
            }
            bool retry = false;
            for (std::size_t side = 0; side < 2; ++side) {
                if (!contact[side]) {
                    continue;
                }
                const std::size_t leg = side == 0 ? 1 : 4;
                const V target = to_model(actor, targets[side]);
                const V pole = position(fk_[roles_[leg + 1]]) + V{0, 0, -0.5};
                animation::TwoBoneSolution solved;
                if (!prepared_[side]->apply(candidate_, target, pole, scratch_, candidate_, solved, error)) {
                    return false;
                }
                if (walking && solved.target_clamped &&
                    norm(solved.end - target) * actor.uniform_scale > .002) {
                    plants[side]->release();
                    contact[side] = false;
                    next[side] = 0;
                    normals[side] = {0, 1, 0};
                    ++probes.released;
                    ++probes.clamped;
                    retry = true;
                    break;
                }
                ++stats.contacts;
                if (solved.target_clamped) {
                    ++stats.clamped;
                }
                if (settings_.align_normals) {
                    const V normal = rotate(math::conjugate(actor.rotation), normals[side]);
                    animation::JointAxisSolution oriented;
                    if (!axes_[side]->apply(candidate_, normal, maximum_angle, scratch_, candidate_, oriented,
                                            error)) {
                        return false;
                    }
                    ++stats.oriented;
                    if (oriented.clamped) {
                        ++stats.clamped;
                    }
                }
                if (settings_.heading && headings[side]) {
                    // Turn the planted foot about the support's normal so its reference axis, laid
                    // in the support plane, points along the held heading: at most
                    // max_heading_degrees. With normal alignment that normal is the foot's up axis,
                    // so the alignment is kept.
                    for (std::size_t i = 0; i < candidate_.size(); ++i) {
                        const auto parent = skeleton_.joints[i].parent;
                        rotations_[i] = math::normalize(parent ? rotations_[*parent] * candidate_[i].rotation
                                                               : candidate_[i].rotation);
                    }
                    const std::size_t joint = roles_[leg + 2];
                    V n = rotate(math::conjugate(actor.rotation), support_normals[side]);
                    n = scale(n, 1.0 / norm(n));
                    V current = rotate(rotations_[joint], local_reference_[side]);
                    current = current - scale(n, dot(n, current));
                    V wanted = rotate(math::conjugate(actor.rotation), *headings[side]);
                    wanted = wanted - scale(n, dot(n, wanted));
                    const double current_length = norm(current), wanted_length = norm(wanted);
                    if (current_length > 1e-6 && wanted_length > 1e-6) {
                        current = scale(current, 1.0 / current_length);
                        wanted = scale(wanted, 1.0 / wanted_length);
                        const double angle = std::atan2(dot(n, cross(current, wanted)), dot(current, wanted));
                        const double bound = settings_.max_heading_degrees * std::numbers::pi / 180.0;
                        const auto turn = math::from_axis_angle(
                            {static_cast<float>(n.x), static_cast<float>(n.y), static_cast<float>(n.z)},
                            static_cast<float>(std::clamp(angle, -bound, bound)));
                        const auto parent = skeleton_.joints[joint].parent;
                        const math::Quat above = parent ? rotations_[*parent] : math::Quat{};
                        candidate_[joint].rotation =
                            math::normalize(math::conjugate(above) * turn * rotations_[joint]);
                        ++stats.headed;
                        if (std::abs(angle) > bound) {
                            ++stats.heading_clamped;
                        }
                    }
                }
            }
            if (!retry) {
                return commit();
            }
        }
        error = "Foot-plant reach retry bound exceeded";
        return false;
    }

private:
    double clearance_ = 0.03, pelvis_drop_ = 0, shorter_leg_ = 0;
    lykoware::character::FootSettings settings_;
    std::array<std::optional<animation::PreparedJointAxis>, 2> axes_{};
    std::array<V, 2> local_reference_{};
    std::array<std::optional<animation::FootPlant>, 2> plants_{};
    std::array<std::uint64_t, 2> plant_cycles_{};
    std::array<V, 2> normals_{{{0, 1, 0}, {0, 1, 0}}};
    std::array<math::Quat, animation::max_joints> rotations_{};
    animation::Skeleton skeleton_;
    std::array<std::optional<animation::PreparedTwoBone>, 2> prepared_;
    animation::TwoBoneScratch scratch_;
    animation::Pose candidate_;
    std::vector<math::Mat4> fk_;
    rig_roles::Indices roles_{};
    std::array<double, 2> correction_{};
    V previous_feet_{};
    bool ready_ = false, previous_ = false;
    Stats stats_{};
};
} // namespace foot_placement
