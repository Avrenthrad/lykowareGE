#pragma once
#include <lykoware/physics.hpp>
#include <optional>
#include <span>
#include <string>
namespace lykoware::animation {
// Where a support body is drawn this frame (ADR 0069): for example blended between its last two
// physics steps, the way the renderer draws it. Feet planted on it are drawn against this pose.
struct DrawnBody {
    physics::BodyId body;
    math::DVec3 position{};
    math::Quat rotation{};
};
// The first entry in `drawn` for `body`, or null.
[[nodiscard]] const DrawnBody* find_drawn(std::span<const DrawnBody> drawn, physics::BodyId body) noexcept;
// ADR 0069. A straight-down probe through `from` hit a body at its physics pose `state`; the body
// is drawn at `drawn`. Returns the hit as drawn: the hit point and normal carried with the body to
// its drawn pose, then the point moved along the drawn surface's tangent plane to straight below
// `from`. nullopt if a value is not finite or the drawn rotation is zero.
[[nodiscard]] std::optional<physics::RayHit> drawn_hit(const physics::RayHit& hit, const physics::BodyState& state,
                                                      const DrawnBody& drawn, math::DVec3 from);
struct FootPlantPolicy {
    double clearance = .03, max_distance = .35, max_slope_degrees = 35;
};
enum class FootPlantStatus { Unplanted, Planted, MissingSurface, TooSteep, SupportLost, TooFar };
struct FootPlantResult {
    math::DVec3 target{};
    math::Vec3 normal{0, 1, 0};
    // World-space unit heading in the support's surface plane, while one is held (ADR 0068):
    // the animated foot's heading when it planted, carried with the support as it moves and turns.
    std::optional<math::Vec3> heading;
    std::optional<physics::BodyId> support;
    FootPlantStatus status = FootPlantStatus::Unplanted;
    bool acquired = false, released = false;
    // The target, normal and heading were resolved against the support's drawn pose (ADR 0069).
    bool drawn = false;
    [[nodiscard]] bool locked() const
    {
        return support.has_value();
    }
};
// ADR0067. Fixed-storage mutable presentation state; caller owns world/body lifetimes.
// One writer; copies permit transactional staging. No heap storage on successful updates.
class FootPlant {
public:
    [[nodiscard]] static std::optional<FootPlant> create(FootPlantPolicy policy, std::string& error);
    // Only a fresh plant edge probes/acquires. Failure/lost support suppresses until lift.
    // Holds body-local point/normal through current support motion. Invalid calls preserve
    // state/output; successful release is a normal result. Separate state per foot/actor.
    // `drawn` (ADR 0069): where moving supports are drawn this frame. A support listed there is
    // resolved, and a fresh plant on it anchored, against its drawn pose instead of its physics
    // state, so the foot stays still on the support as drawn. The physics world still decides
    // whether the support exists. A listed pose that is used must be finite with a non-zero
    // rotation, or the call is refused. Unlisted supports, and an empty list, behave as before.
    [[nodiscard]] bool update(const physics::World& world, math::DVec3 animated_foot, bool planted,
                              FootPlantResult& out, std::string& error, std::span<const DrawnBody> drawn = {});
    // As above, also holding a heading (ADR 0068). On a fresh plant the animated heading (a finite,
    // non-zero world direction) is projected onto the support's surface plane and kept in support
    // coordinates; while held, results carry it through the support's motion. A heading along the
    // surface normal holds none. The animated heading is ignored while held.
    [[nodiscard]] bool update(const physics::World& world, math::DVec3 animated_foot, math::Vec3 animated_heading,
                              bool planted, FootPlantResult& out, std::string& error,
                              std::span<const DrawnBody> drawn = {});
    [[nodiscard]] bool locked() const noexcept
    {
        return support_.has_value();
    }
    void reset() noexcept
    {
        support_.reset();
        local_heading_.reset();
        armed_ = true;
    }
    void release() noexcept
    {
        support_.reset();
        local_heading_.reset();
        armed_ = false;
    }

private:
    FootPlant() = default;
    FootPlantPolicy policy_;
    std::optional<physics::BodyId> support_;
    math::DVec3 local_point_{};
    math::Vec3 local_normal_{0, 1, 0};
    std::optional<math::Vec3> local_heading_;
    bool armed_ = true;
    [[nodiscard]] bool step(const physics::World& world, math::DVec3 animated_foot,
                            std::optional<math::Vec3> animated_heading, bool planted, FootPlantResult& out,
                            std::string& error, std::span<const DrawnBody> drawn);
};
} // namespace lykoware::animation
