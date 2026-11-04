#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>

// The Lykoware physics API (ADR 0019) over vendored Jolt Physics (ADR 0003, ADR 0018).
// No Jolt type appears here; game and tool code use only these types.
//
// Units and axes: metres, kilograms, seconds, radians; +Y is up; right-handed. Gravity is a
// world setting (default 9.81 m/s^2 down).

namespace lykoware::physics {

// Minimal value types until the engine has a maths module.
struct Vec3 {
    float x = 0.F;
    float y = 0.F;
    float z = 0.F;
};

// Unit quaternion (x, y, z, w); the default is no rotation.
struct Quat {
    float x = 0.F;
    float y = 0.F;
    float z = 0.F;
    float w = 1.F;
};

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

using Shape = std::variant<BoxShape, SphereShape, CapsuleShape>;

struct BodyDesc {
    Shape shape = BoxShape{};
    MotionType motion = MotionType::Dynamic;
    Vec3 position;
    Quat rotation;
    Vec3 linear_velocity;
    Vec3 angular_velocity;
    float friction = 0.5F;
    float restitution = 0.F;
};

struct BodyState {
    Vec3 position;
    Quat rotation;
    Vec3 linear_velocity;
    Vec3 angular_velocity;
};

struct RayHit {
    BodyId body;
    // Fraction of the ray's length at which it hits, in [0, 1].
    float fraction = 0.F;
    Vec3 point;
};

struct WorldConfig {
    std::uint32_t max_bodies = 4096;
    std::uint32_t max_body_pairs = 8192;
    std::uint32_t max_contact_constraints = 8192;
    Vec3 gravity{0.F, -9.81F, 0.F};
    // Worker threads for the physics step; 0 picks a number from the CPU. Results do not depend
    // on this number.
    std::uint32_t worker_threads = 0;
    // Scratch memory for one step.
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
    // with a non-positive size, a zero rotation, or when max_bodies are alive. The rotation is
    // normalised.
    [[nodiscard]] std::optional<BodyId> create_body(const BodyDesc& desc, std::string& error);
    // Removes and destroys a body. False if the handle is not valid.
    bool destroy_body(BodyId body);
    [[nodiscard]] bool is_valid(BodyId body) const;
    [[nodiscard]] std::size_t body_count() const;

    [[nodiscard]] std::optional<BodyState> state(BodyId body) const;
    // Sets velocities, waking the body. False if the handle is not valid or the body is static.
    bool set_velocity(BodyId body, Vec3 linear, Vec3 angular);

    // Advances the world by one fixed step of `seconds` (> 0). False if the step failed, for
    // example because it ran out of step memory or contact space; the world stays usable.
    bool step(float seconds);

    // The closest body hit by the ray from `origin` along `direction`, whose length is the
    // ray's length; nullopt if nothing is hit.
    [[nodiscard]] std::optional<RayHit> cast_ray(Vec3 origin, Vec3 direction) const;

    // FNV-1a hash of every live body's exact state in handle order: equal hashes mean
    // bit-identical simulations.
    [[nodiscard]] std::uint64_t state_hash() const;

private:
    struct Impl;
    explicit World(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}
