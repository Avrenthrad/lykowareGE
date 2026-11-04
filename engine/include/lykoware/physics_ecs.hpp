#pragma once

#include <lykoware/ecs.hpp>
#include <lykoware/physics.hpp>
#include <lykoware/transform.hpp>

#include <string>

// Physics bodies and characters on ECS entities (ADR 0023). An entity with a RigidBody or
// Character component owns that physics object: removing the component, or destroying the
// entity, destroys it. The physics world drives motion; step() copies the results into each
// entity's Transform.

namespace lykoware::physics_ecs {

// Where an entity is (lykoware/transform.hpp). Filled from physics by Link::sync(); a
// character's rotation is always the identity (it stays upright).
using Transform = lykoware::Transform;

// The physics body an entity owns.
struct RigidBody {
    physics::BodyId id;
};

// The physics character an entity owns.
struct Character {
    physics::CharacterId id;
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
// One physics world per ECS world: a second link replaces the first one's hooks. Do not move a
// linked ECS world. Threading and failure rules are those of the two worlds.
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
    // the entity is not alive, already has a RigidBody, the ECS world is locked, or physics
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
