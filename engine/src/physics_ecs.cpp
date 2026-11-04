#include <lykoware/physics_ecs.hpp>

#include <optional>
#include <memory>
#include <stdexcept>

namespace lykoware::physics_ecs {

Link::Link(ecs::World& entities, physics::World& physics) : entities_(entities), physics_(physics)
{
    // One shared cleanup context retained by both pools, even after this helper dies.
    auto context = std::make_shared<physics::World*>(&physics_);
    if (!entities_.claim_removal_hooks<RigidBody, Character>(
            context,
            [context](ecs::Entity, RigidBody& body) { (*context)->destroy_body(body.id()); },
            [context](ecs::Entity, Character& character) { (*context)->destroy_character(character.id()); })) {
        throw std::logic_error("physics link requires an unlocked, unclaimed ECS world");
    }
}

bool Link::add_body(ecs::Entity entity, const physics::BodyDesc& desc, std::string& error)
{
    if (entities_.locked()) {
        error = "the ECS world is locked (inside each() or a removal hook)";
        return false;
    }
    if (!entities_.alive(entity)) {
        error = "the entity is not alive";
        return false;
    }
    if (entities_.has<RigidBody>(entity) || entities_.has<Character>(entity)) {
        error = "the entity already has a physics body";
        return false;
    }
    const std::optional<physics::BodyId> id = physics_.create_body(desc, error);
    if (!id) {
        return false;
    }
    const std::optional<physics::BodyState> state = physics_.state(*id);
    const Transform transform{state->position, state->rotation};
    const bool had_transform = entities_.has<Transform>(entity);
    try {
        if (!had_transform) {
            entities_.add<Transform>(entity, transform);
        }
        entities_.add<RigidBody>(entity, RigidBody{*id});
    } catch (...) {
        if (!had_transform) {
            entities_.remove<Transform>(entity);
        }
        physics_.destroy_body(*id);
        throw;
    }
    *entities_.get<Transform>(entity) = transform;
    error.clear();
    return true;
}

bool Link::add_character(ecs::Entity entity, const physics::CharacterDesc& desc, std::string& error)
{
    if (entities_.locked()) {
        error = "the ECS world is locked (inside each() or a removal hook)";
        return false;
    }
    if (!entities_.alive(entity)) {
        error = "the entity is not alive";
        return false;
    }
    if (entities_.has<RigidBody>(entity) || entities_.has<Character>(entity)) {
        error = "the entity already has a physics character";
        return false;
    }
    const std::optional<physics::CharacterId> id = physics_.create_character(desc, error);
    if (!id) {
        return false;
    }
    const std::optional<physics::CharacterState> state = physics_.character_state(*id);
    const Transform transform{state->position, physics::Quat{}};
    const bool had_transform = entities_.has<Transform>(entity);
    try {
        if (!had_transform) {
            entities_.add<Transform>(entity, transform);
        }
        entities_.add<Character>(entity, Character{*id});
    } catch (...) {
        if (!had_transform) {
            entities_.remove<Transform>(entity);
        }
        physics_.destroy_character(*id);
        throw;
    }
    *entities_.get<Transform>(entity) = transform;
    error.clear();
    return true;
}

void Link::sync()
{
    entities_.each<RigidBody, Transform>([&](ecs::Entity, RigidBody& body, Transform& transform) {
        if (const std::optional<physics::BodyState> state = physics_.state(body.id())) {
            transform = Transform{state->position, state->rotation};
        }
    });
    entities_.each<Character, Transform>([&](ecs::Entity, Character& character, Transform& transform) {
        if (const std::optional<physics::CharacterState> state = physics_.character_state(character.id())) {
            transform = Transform{state->position, physics::Quat{}};
        }
    });
}

bool Link::step(float seconds)
{
    const bool stepped = physics_.step(seconds);
    if (stepped) {
        sync();
    }
    return stepped;
}

}
