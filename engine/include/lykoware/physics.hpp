#pragma once

#include <lykoware/math.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

// The Lykoware physics API (ADR 0020) over vendored Jolt Physics (ADR 0003, ADR 0018).
// No Jolt type appears here; game and tool code use only these types.
//
// Units and axes: metres, kilograms, seconds, radians; +Y is up; right-handed. Gravity is a
// world setting (default 9.81 m/s^2 down).
//
// Positions are double precision (DVec3) so they stay accurate to far below a millimetre
// anywhere in a world of 16 km or more (ADR 0021, ADR 0022). Velocities, directions, sizes
// and rotations are single precision.

namespace lykoware::physics {

// Value types from the engine's maths module (ADR 0024). The aliases keep physics code and
// its callers unchanged.
using Vec3 = math::Vec3;
using DVec3 = math::DVec3;
using Quat = math::Quat;

// Stable handle to a body. A destroyed body's handle stays invalid for the rest of the world's
// life: its slot can be reused, but with a new generation, so an old handle never reaches a
// new body. The default handle is never valid.
struct BodyId {
    std::uint32_t index = 0;
    std::uint32_t generation = 0;

    friend bool operator==(const BodyId&, const BodyId&) = default;
};

enum class MotionType {
    Static,    // never moves; costs nothing when nothing touches it
    Dynamic,   // moved by forces, gravity and contacts
    Kinematic, // moved only by its velocity; pushes dynamic bodies, is not pushed
};

struct BoxShape {
    Vec3 half_extents{0.5F, 0.5F, 0.5F};
};

struct SphereShape {
    float radius = 0.5F;
};

// A capsule along the body's Y axis: a cylinder of 2 * half_height with hemispherical caps.
struct CapsuleShape {
    float half_height = 0.5F;
    float radius = 0.3F;
};

// Terrain: a square grid of samples_per_side x samples_per_side heights in metres, row-major
// (the sample at column x, row z is heights[z * samples_per_side + x]), spaced `spacing` metres
// apart along +X and +Z from the body's position. Static bodies only. samples_per_side must be
// even, from 4 to 4096. Heights are stored compressed to within 1 cm.
struct HeightFieldShape {
    std::vector<float> heights;
    std::uint32_t samples_per_side = 0;
    float spacing = 1.F;
};

using Shape = std::variant<BoxShape, SphereShape, CapsuleShape, HeightFieldShape>;

struct BodyDesc {
    Shape shape = BoxShape{};
    MotionType motion = MotionType::Dynamic;
    DVec3 position;
    Quat rotation;
    Vec3 linear_velocity;
    Vec3 angular_velocity;
    float friction = 0.5F;
    float restitution = 0.F;
};

struct BodyState {
    DVec3 position;
    Quat rotation;
    Vec3 linear_velocity;
    Vec3 angular_velocity;
};

// Stable handle to a character, with the same rules as BodyId.
struct CharacterId {
    std::uint32_t index = 0;
    std::uint32_t generation = 0;

    friend bool operator==(const CharacterId&, const CharacterId&) = default;
};

// A walking character: an upright capsule moved by its input, gravity and the ground, colliding
// with bodies and terrain. Its position is its feet (the bottom of the capsule).
struct CharacterDesc {
    float radius = 0.3F;
    // Half the length of the capsule's straight part; standing height is 2 * (cylinder_half_height + radius).
    float cylinder_half_height = 0.6F;
    DVec3 position;
    // Steepest ground it can walk up, in radians (0, pi/2). Steeper ground is OnSteepGround.
    float max_slope = 0.87266463F; // 50 degrees
    // Tallest step or ledge it walks up without jumping, in metres (>= 0).
    float step_height = 0.4F;
    // Mass in kilograms, used when it pushes dynamic bodies.
    float mass = 70.F;
};

enum class GroundState {
    OnGround,      // standing on walkable ground
    OnSteepGround, // touching ground too steep to walk up; it slides down
    NotSupported,  // touching something that does not hold it up
    InAir,         // touching nothing
};

struct CharacterState {
    DVec3 position; // feet
    Vec3 velocity;
    GroundState ground = GroundState::InAir;
    // The body it stands on or touches, if any.
    std::optional<BodyId> ground_body;
    Vec3 ground_normal;
};

struct RayHit {
    BodyId body;
    // Fraction of the ray's length at which it hits, in [0, 1].
    float fraction = 0.F;
    DVec3 point;
};

struct WorldConfig {
    std::uint32_t max_bodies = 4096;
    std::uint32_t max_body_pairs = 8192;
    std::uint32_t max_contact_constraints = 8192;
    Vec3 gravity{0.F, -9.81F, 0.F};
    // Worker threads for the physics step; 0 picks a number from the CPU. Results do not depend
    // on this number.
    std::uint32_t worker_threads = 0;
    // Fast scratch memory reserved for one step, from 64 KiB to 1 GiB. A step that needs more
    // takes it from the heap (slower) rather than failing.
    std::size_t step_memory_bytes = 16U * 1024U * 1024U;
};

// One physics world.
//
// Determinism: the same calls in the same order give bit-identical results on every supported
// compiler, OS and build type (Jolt's cross-platform determinism, ADR 0003). Step once per
// simulation tick with FixedStep::tick_seconds() (ADR 0017). state_hash() checks it.
//
// Ownership: create() returns the sole owner. The world owns its bodies; destroying the world
// destroys them. Copy and move are not provided.
//
// Threading: one thread at a time. Several worlds may exist; create and destroy them on one
// thread, because the first world sets up and the last tears down Jolt's process-wide state.
//
// Failure: create() returns nullptr and fills error for invalid limits. Every function that
// takes a BodyId returns false or nullopt for an invalid, destroyed or foreign handle and does
// nothing else. Nothing throws for those; std::bad_alloc can propagate.
class World {
public:
    [[nodiscard]] static std::unique_ptr<World> create(const WorldConfig& config, std::string& error);

    ~World();
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World(World&&) = delete;
    World& operator=(World&&) = delete;

    // Creates a body and adds it to the simulation. Fails (nullopt, error filled) for a shape
    // with a non-positive size, an invalid height field or a height field that is not static,
    // a non-finite position or velocity, a zero rotation, or when max_bodies are alive. The
    // rotation is normalised.
    [[nodiscard]] std::optional<BodyId> create_body(const BodyDesc& desc, std::string& error);
    // Removes and destroys a body. False if the handle is not valid.
    bool destroy_body(BodyId body);
    [[nodiscard]] bool is_valid(BodyId body) const;
    [[nodiscard]] std::size_t body_count() const;

    [[nodiscard]] std::optional<BodyState> state(BodyId body) const;
    // Sets velocities, waking the body. False if the handle is not valid or the body is static.
    bool set_velocity(BodyId body, Vec3 linear, Vec3 angular);

    // Characters. Same handle rules and failure behaviour as bodies; create_character fails for
    // non-positive sizes or mass, a slope outside (0, pi/2) or a negative step height.
    [[nodiscard]] std::optional<CharacterId> create_character(const CharacterDesc& desc, std::string& error);
    bool destroy_character(CharacterId character);
    [[nodiscard]] bool is_valid(CharacterId character) const;
    [[nodiscard]] std::size_t character_count() const;
    // The horizontal velocity the character tries to move at from now on (the vertical part is
    // ignored), and an optional jump speed used once, on the next step where it is on walkable
    // ground. Gravity and moving ground are added by the world.
    bool set_character_input(CharacterId character, Vec3 horizontal_velocity, float jump_speed = 0.F);
    [[nodiscard]] std::optional<CharacterState> character_state(CharacterId character) const;

    // Advances the world by one fixed step of `seconds` (> 0): characters move first, in handle
    // order, then bodies. False if the step failed because it ran out of body-pair or contact
    // space (raise max_body_pairs or max_contact_constraints); the world stays usable.
    bool step(float seconds);

    // The closest body hit by the ray from `origin` along `direction`, whose length is the
    // ray's length; nullopt if nothing is hit.
    [[nodiscard]] std::optional<RayHit> cast_ray(DVec3 origin, Vec3 direction) const;

    // FNV-1a hash of every live body's and character's exact state in handle order: equal
    // hashes mean bit-identical simulations.
    [[nodiscard]] std::uint64_t state_hash() const;

private:
    struct Impl;
    explicit World(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}
