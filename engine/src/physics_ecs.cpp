#include <lykoware/physics_ecs.hpp>

#include <optional>

namespace lykoware::physics_ecs {

Link::Link(ecs::World& entities, physics::World& physics) : entities_(entities), physics_(physics)
{
    physics::World* world = &physics_;
    entities_.on_remove<RigidBody>([world](ecs::Entity, RigidBody& body) { world->destroy_body(body.id); });
    entities_.on_remove<Character>(
        [world](ecs::Entity, Character& character) { world->destroy_character(character.id); });
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
    if (entities_.has<RigidBody>(entity)) {
        error = "the entity already has a physics body";
        return false;
    }
    const std::optional<physics::BodyId> id = physics_.create_body(desc, error);
    if (!id) {
        return false;
    }
    const std::optional<physics::BodyState> state = physics_.state(*id);
    entities_.add<RigidBody>(entity, RigidBody{*id});
    const Transform transform{state->position, state->rotation};
    if (Transform* existing = entities_.get<Transform>(entity)) {
        *existing = transform;
    } else {
        entities_.add<Transform>(entity, transform);
    }
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
    if (entities_.has<Character>(entity)) {
        error = "the entity already has a physics character";
        return false;
    }
    const std::optional<physics::CharacterId> id = physics_.create_character(desc, error);
    if (!id) {
        return false;
    }
    const std::optional<physics::CharacterState> state = physics_.character_state(*id);
    entities_.add<Character>(entity, Character{*id});
    const Transform transform{state->position, physics::Quat{}};
    if (Transform* existing = entities_.get<Transform>(entity)) {
        *existing = transform;
    } else {
        entities_.add<Transform>(entity, transform);
    }
    return true;
}

void Link::sync()
{
    entities_.each<RigidBody, Transform>([&](ecs::Entity, RigidBody& body, Transform& transform) {
        if (const std::optional<physics::BodyState> state = physics_.state(body.id)) {
            transform = Transform{state->position, state->rotation};
        }
    });
    entities_.each<Character, Transform>([&](ecs::Entity, Character& character, Transform& transform) {
        if (const std::optional<physics::CharacterState> state = physics_.character_state(character.id)) {
            transform = Transform{state->position, physics::Quat{}};
        }
    });
}

bool Link::step(float seconds)
{
    const bool stepped = physics_.step(seconds);
    sync();
    return stepped;
}

}
