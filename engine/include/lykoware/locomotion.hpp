#pragma once

#include <lykoware/math.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Locomotion motor (ADR 0074): turns "the velocity I want" into "the velocity to ask physics
// for, and how the body is carried", for players and NPCs alike.
//
// The motor knows nothing about input, cameras, physics or animation. Its caller gives it a
// wanted horizontal velocity each fixed step and passes the answer to its physics character;
// physics keeps authority over where the character really goes (ADR 0064).
//
// Units and axes: metres, seconds, +Y up. Angles in data are degrees; angles in State are
// radians. A yaw is FollowCamera's: 0 faces -Z, and the yaw of a direction d is
// atan2(-d.x, -d.z).

namespace lykoware::locomotion {

enum class Gait { Idle, Walk, Jog, Run, Sprint };

// What the legs are doing, for animation to follow.
enum class Legs {
    Stopped,  // standing still
    Moving,   // travelling, speeding up or steering
    Braking,  // slowing because less speed, or none, is wanted
    Pivoting, // a redirect too tight to carry: braking to plant, then pushing off the new way
    Airborne, // not on walkable ground
};

[[nodiscard]] const char* name(Gait gait);
[[nodiscard]] const char* name(Legs legs);

// One concrete set of movement limits. A Profile holds two of these, heavy and sharp, and
// agility picks a point between them.
struct Tuning {
    float acceleration = 0.F;       // m/s^2 when speeding up
    float braking = 0.F;            // m/s^2 when slowing or stopping
    float pivot_braking = 0.F;      // m/s^2 when braking into a pivot
    float turn_rate_rest = 0.F;     // degrees/second the heading can turn at a standstill
    float turn_rate_sprint = 0.F;   // degrees/second at sprint speed; between, a straight line
    float pivot_angle = 0.F;        // degrees: a redirect wider than this brakes first
    float pivot_speed = 0.F;        // m/s: how slow a pivot brakes before it turns
    float facing_rate = 0.F;        // degrees/second the body turns to face
    float gait_change_seconds = 0.F; // shortest time in one gait before the next change
    float lean = 0.F;               // radians of lean per m/s^2, before the bound
};

// Names of Tuning's fields in data, in declaration order.
inline constexpr std::array<const char*, 10> tuning_keys{
    "acceleration", "braking",     "pivot_braking", "turn_rate_rest",      "turn_rate_sprint",
    "pivot_angle",  "pivot_speed", "facing_rate",   "gait_change_seconds", "lean"};

// A named variant of a profile, such as "combat": its own agility and any fields set directly.
struct Variant {
    std::string name;
    float agility = 0.5F;
    // Fields set directly, by tuning_keys index; these win over agility.
    std::array<std::optional<float>, 10> overrides{};
};

// Everything that shapes one character's movement. Held in data (version 1 JSON, ADR 0004).
struct Profile {
    // Speeds of walk, jog, run and sprint, in m/s, strictly rising.
    std::array<float, 4> gait_speeds{1.6F, 3.2F, 5.0F, 7.0F};
    // A gait changes only once the speed is this fraction of the gap past the midpoint, so it
    // does not flicker.
    float gait_margin = 0.15F;
    // Agility 0 uses `heavy`, 1 uses `sharp`, and between them each field is a straight line.
    Tuning heavy{6.F, 9.F, 12.F, 360.F, 70.F, 150.F, 1.2F, 360.F, 0.35F, 0.012F};
    Tuning sharp{24.F, 34.F, 48.F, 1080.F, 260.F, 75.F, 2.2F, 1080.F, 0.10F, 0.006F};
    // The variant used unless another is selected by name.
    Variant base{"default", 0.5F, {}};
    std::vector<Variant> variants;
    // How fast the horizontal velocity may be steered in the air, m/s^2.
    float air_control = 2.F;
    // With a wanted facing (strafing, aiming): the fraction of the wanted speed kept when moving
    // straight sideways, and straight backward. In between, it changes smoothly with the angle.
    float strafe_scale = 0.9F;
    float backward_scale = 0.75F;
    // The most the body leans, radians, and how quickly lean follows acceleration, seconds.
    float max_lean = 0.25F;
    float lean_seconds = 0.15F;
};

// True when every number is finite and in range, speeds rise, and variant names are unique and
// not empty; otherwise false with `error` filled.
[[nodiscard]] bool valid(const Profile& profile, std::string& error);

// The limits a variant gives: each field a straight line from heavy to sharp by its agility,
// then its overrides. `variant` empty means the base variant. nullopt for an unknown name.
[[nodiscard]] std::optional<Tuning> resolve(const Profile& profile, std::string_view variant = {});

// Reads a profile from version 1 JSON. Missing parts keep their defaults; unknown keys, wrong
// types, duplicate keys and out-of-range values are refused, with `error` filled and `out`
// untouched. At most 64 KiB.
//   {"version":1, "agility":0.5,
//    "gaits":{"walk":1.6,"jog":3.2,"run":5.0,"sprint":7.0}, "gait_margin":0.15,
//    "heavy":{...Tuning...}, "sharp":{...Tuning...}, "overrides":{...Tuning...},
//    "air_control":2.0, "max_lean":0.25, "lean_seconds":0.15,
//    "strafe_scale":0.9, "backward_scale":0.75,
//    "variants":{"combat":{"agility":0.85,"overrides":{...}}}}
[[nodiscard]] bool decode_profile(std::string_view json, Profile& out, std::string& error);

// What a character wants for the coming step.
struct Request {
    // Wanted horizontal velocity in world space; y is ignored. Its length is the wanted speed,
    // limited to the sprint speed.
    math::Vec3 velocity{};
    // Whether the character stands on walkable ground.
    bool grounded = true;
    // The horizontal velocity physics actually gave the character last step, if known. When it
    // is slower than the motor believed (a wall, a slope), the motor continues from it instead
    // of building up speed against the obstacle.
    std::optional<math::Vec3> actual_velocity;
    // The way the body should face, as a yaw in radians (aiming, strafing, a locked-on target).
    // Without one the body turns to face the way it travels. With one it turns to face this at
    // its facing rate and keeps facing it while it moves any way: sideways and backward speeds
    // are scaled by strafe_scale and backward_scale.
    std::optional<float> facing;
};

struct State {
    // The velocity to ask physics for; y is always 0.
    math::Vec3 velocity{};
    float speed = 0.F; // its length
    // The way the character travels, and the way its body faces, as yaws in radians.
    float heading = 0.F;
    float facing = 0.F;
    Gait gait = Gait::Idle;
    Legs legs = Legs::Stopped;
    // Lean in radians, in the body's own frame (the way it faces): forward is positive when
    // speeding up; side is positive leaning to the body's right. Each within the profile's max_lean.
    float lean_forward = 0.F;
    float lean_side = 0.F;
    // The way the character travels relative to the way the body faces, in radians: 0 forward,
    // pi/2 to the body's left, -pi/2 to its right, +-pi backward. Continuous: animation blends
    // directions by it rather than picking one of eight.
    float travel = 0.F;
    // Whether the latest request held a wanted facing.
    bool facing_held = false;
    // Counts pivots begun, so a caller can act once on each.
    std::uint64_t pivots = 0;
};

// One character's motor.
//
// Ownership: a value type holding its own copy of the profile; copy and move freely.
// Threading: one thread at a time per motor; separate motors are independent.
// Determinism: the same requests and step lengths in the same order give bit-identical states
// in one build on one machine (it uses sin, cos and atan2, as PlayerCharacter does).
// Failure: create() returns nullopt with `error` filled for an invalid profile. step() with a
// non-finite or non-positive length, or a non-finite request, changes nothing.
// Allocation: step() allocates nothing.
class Motor {
public:
    [[nodiscard]] static std::optional<Motor> create(const Profile& profile, std::string& error);

    // Stops dead, facing `facing`, in the idle gait. The selected variant is kept.
    void reset(float facing = 0.F);
    // Selects a variant by name (empty for the base). False, changing nothing, if unknown.
    bool select(std::string_view variant);
    // Sets the selected variant's agility (clamped to 0..1) and recomputes its limits. Overrides
    // stay. Non-finite values are ignored.
    void set_agility(float agility);

    // Advances one fixed step of `seconds` and returns the new state.
    const State& step(const Request& request, float seconds);

    [[nodiscard]] const State& state() const { return state_; }
    [[nodiscard]] const Tuning& tuning() const { return tuning_; }
    [[nodiscard]] const Profile& profile() const { return profile_; }
    [[nodiscard]] float agility() const;
    // The speed of a gait in m/s (0 for idle).
    [[nodiscard]] float gait_speed(Gait gait) const;

private:
    Motor() = default;
    void retune();

    Profile profile_;
    Tuning tuning_;
    std::size_t selected_ = 0; // 0 is the base variant; n is variants[n - 1]
    State state_;
    math::Vec3 direction_{0.F, 0.F, -1.F}; // unit heading
    float gait_seconds_ = 0.F;
    bool pivoting_ = false;
};

}
