#pragma once

#include <lykoware/ecs.hpp>
#include <lykoware/physics.hpp>

#include <string>
#include <utility>

// Physics bodies and characters on ECS entities (ADR 0023). An entity with a RigidBody or
// Character component owns that physics object: removing the component, or destroying the
// entity, destroys it. The physics world drives motion; step() copies the results into each
// entity's Transform.

namespace lykoware::physics_ecs {

// Where an entity is. Filled from physics by Link::sync(). It moves to a general scene module
// when the renderer needs it. A character's rotation is always the identity (it stays upright).
struct Transform {
    physics::DVec3 position;
    physics::Quat rotation;
};

// Unique ownership components: created by Link, inspected through id(). Do not move a
// component out of ECS storage; moves are for packed-pool relocation only.
class RigidBody {
public:
    RigidBody(const RigidBody&) = delete;
    RigidBody& operator=(const RigidBody&) = delete;
    RigidBody(RigidBody&& other) noexcept : id_(std::exchange(other.id_, {})) {}
    RigidBody& operator=(RigidBody&& other) noexcept
    {
        id_ = std::exchange(other.id_, {});
        return *this;
    }
    [[nodiscard]] physics::BodyId id() const { return id_; }
private:
    friend class Link;
    explicit RigidBody(physics::BodyId id) : id_(id) {}
    physics::BodyId id_;
};

class Character {
public:
    Character(const Character&) = delete;
    Character& operator=(const Character&) = delete;
    Character(Character&& other) noexcept : id_(std::exchange(other.id_, {})) {}
    Character& operator=(Character&& other) noexcept
    {
        id_ = std::exchange(other.id_, {});
        return *this;
    }
    [[nodiscard]] physics::CharacterId id() const { return id_; }
private:
    friend class Link;
    explicit Character(physics::CharacterId id) : id_(id) {}
    physics::CharacterId id_;
};

// Connects one ECS world to one physics world.
//
// Creating a link installs removal hooks in the ECS world: from then on, removing a RigidBody
// or Character component destroys its physics object, whether by remove(), destroy(), or the
// ECS world's own destruction. The hooks stay after the link object is gone.
//
// Lifetime: the physics world must outlive the ECS world (create it first). The link object
// itself is only a helper; use it while both worlds exist.
//
// One exclusive link per ECS world: rebinding, conflicting hooks, preexisting ownership
// components, or construction while locked throw std::logic_error. The claim persists after
// helper destruction. Moving a linked world is refused. Threading follows the two worlds.
class Link {
public:
    Link(ecs::World& entities, physics::World& physics);
    ~Link() = default;
    Link(const Link&) = delete;
    Link& operator=(const Link&) = delete;
    Link(Link&&) = delete;
    Link& operator=(Link&&) = delete;

    // Creates a physics body from desc and gives the entity RigidBody and Transform components
    // (an existing Transform is overwritten). False, with error filled and nothing changed, if
    // the entity is not alive, already has either ownership component, is locked, or physics
    // refuses the body.
    bool add_body(ecs::Entity entity, const physics::BodyDesc& desc, std::string& error);
    // The same for a walking character and the Character component.
    bool add_character(ecs::Entity entity, const physics::CharacterDesc& desc, std::string& error);

    // Copies every linked body's and character's position (and a body's rotation) into its
    // Transform. Entities whose Transform was removed are skipped.
    void sync();

    // Steps the physics world by `seconds`, then sync(). False if the physics step failed.
    bool step(float seconds);

private:
    ecs::World& entities_;
    physics::World& physics_;
};

}
