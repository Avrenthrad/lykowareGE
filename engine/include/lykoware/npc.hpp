#pragma once

#include <lykoware/locomotion.hpp>
#include <lykoware/math.hpp>
#include <lykoware/physics.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>

// A non-player character that moves like the player: its own decision (a wanted velocity)
// goes through the same locomotion motor and the same physics character as PlayerCharacter's
// (ADR 0074, step 5), with its own agility. What it wants, and why, is the caller's business;
// this class only carries it out.

namespace lykoware {

struct NpcConfig {
    // The character's capsule, spawn point (its feet), walkable slope and step height.
    physics::CharacterDesc body;
    // The movement limits (heavy and sharp tunings, gaits, variants). The selected variant's
    // agility is this NPC's own.
    locomotion::Profile locomotion;
};

// A wanted horizontal velocity for walking from `from` towards `to` at `speed` m/s: the
// horizontal direction to the target, at full speed outside `arrive_radius` and, inside it,
// in proportion to the remaining distance, so it is zero at the target. Heights are ignored.
// Zero for a non-finite input, a non-positive speed, or when already at the target. An
// arrive_radius of 0 (or less) never slows: use it for waypoints to pass through. Allocates
// nothing; a pure function, safe from any thread.
[[nodiscard]] math::Vec3 seek(const math::DVec3& from, const math::DVec3& to, float speed, float arrive_radius);

// A character in a physics world that decides for itself where to go.
//
// Each fixed step: want() with the latest decision (it stays in force until replaced), then
// before_step() before the physics step. The motor limits how fast the real velocity may
// change, so a want that reverses at once becomes a braked turn.
//
// As for the player, the motor is told how far the feet really moved over the previous step
// (measured from positions, since the world reports the velocity it was asked for), so a wall
// or a slope does not make it build up speed it never gets.
//
// Drawing between steps (ADR 0048): pass the frame's FrameSteps::alpha to drawn_feet() and
// drawn_facing(). Presentation only: alpha never reaches the simulation.
//
// Ownership: create() returns the sole owner. It owns its character in the world and destroys
// it with itself; the world must outlive it. Copy and move are not provided.
//
// Threading: one thread at a time, with no physics step running.
//
// Determinism: the same wants and steps in the same order give bit-identical characters.
//
// Failure: create() returns nullptr with `error` filled for an invalid locomotion profile or
// when the world refuses the character; nothing is left in the world then. want() ignores
// non-finite values. before_step() with a non-finite or non-positive length keeps the motor's
// state and sends the world the velocity it already had.
//
// Allocation: want() and before_step() allocate nothing.
class NpcCharacter {
public:
    [[nodiscard]] static std::unique_ptr<NpcCharacter> create(physics::World& world, const NpcConfig& config,
                                                              std::string& error);

    ~NpcCharacter();
    NpcCharacter(const NpcCharacter&) = delete;
    NpcCharacter& operator=(const NpcCharacter&) = delete;
    NpcCharacter(NpcCharacter&&) = delete;
    NpcCharacter& operator=(NpcCharacter&&) = delete;

    // The NPC's decision: the horizontal velocity it wants in world space (y is ignored; its
    // length, limited to the sprint speed, is the wanted speed), and optionally a yaw (radians,
    // FollowCamera's convention) to face while it moves, as the player does when aiming
    // (locomotion::Request::facing). Omitting the yaw has the body face its travel. If either
    // value is non-finite the whole call is ignored.
    void want(math::Vec3 velocity, std::optional<float> facing = std::nullopt);
    // Asks the motor for the coming physics step of `seconds` and passes its velocity to the
    // world.
    void before_step(float seconds);

    [[nodiscard]] physics::CharacterId character() const { return character_; }
    [[nodiscard]] physics::CharacterState state() const;
    // The motor's state after the latest before_step().
    [[nodiscard]] const locomotion::State& locomotion() const { return motor_.state(); }
    // The way the body faces, as a yaw.
    [[nodiscard]] float facing() const { return facing_; }
    // Where to draw the feet and the body: `alpha` (clamped to 0..1) of the way from before the
    // latest step to after it. A non-finite alpha gives the latest step.
    [[nodiscard]] math::DVec3 drawn_feet(float alpha) const;
    [[nodiscard]] float drawn_facing(float alpha) const;
    // Selects a variant of the profile by name (empty for the base). False, changing nothing,
    // if unknown.
    bool select_locomotion(std::string_view variant);
    // Sets the selected variant's agility (clamped to 0..1). False for a non-finite value.
    bool set_agility(float agility);

private:
    NpcCharacter(physics::World& world, physics::CharacterId character, locomotion::Motor motor);

    physics::World& world_;
    physics::CharacterId character_;
    locomotion::Motor motor_;
    math::Vec3 wanted_{};
    std::optional<float> look_;
    float facing_ = 0.F;
    float previous_facing_ = 0.F;
    // The feet before the latest step, for drawing between steps.
    math::DVec3 previous_feet_;
    // The length of the latest step, to know how fast the feet really moved in it.
    float previous_seconds_ = 0.F;
};

}
