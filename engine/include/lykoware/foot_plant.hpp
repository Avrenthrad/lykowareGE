#pragma once
#include <lykoware/physics.hpp>
#include <optional>
#include <string>
namespace lykoware::animation {
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
    [[nodiscard]] bool update(const physics::World& world, math::DVec3 animated_foot, bool planted,
                              FootPlantResult& out, std::string& error);
    // As above, also holding a heading (ADR 0068). On a fresh plant the animated heading (a finite,
    // non-zero world direction) is projected onto the support's surface plane and kept in support
    // coordinates; while held, results carry it through the support's motion. A heading along the
    // surface normal holds none. The animated heading is ignored while held.
    [[nodiscard]] bool update(const physics::World& world, math::DVec3 animated_foot, math::Vec3 animated_heading,
                              bool planted, FootPlantResult& out, std::string& error);
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
                            std::string& error);
};
} // namespace lykoware::animation
