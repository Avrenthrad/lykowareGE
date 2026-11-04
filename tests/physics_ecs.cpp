// physics.ecs_link: physics bodies and characters on ECS entities (ADR 0023). Links only
// lykoware::engine.

#include <lykoware/clock.hpp>
#include <lykoware/ecs.hpp>
#include <lykoware/physics.hpp>
#include <lykoware/physics_ecs.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

namespace ecs = lykoware::ecs;
namespace physics = lykoware::physics;
namespace link = lykoware::physics_ecs;

int g_failures = 0;

void expect(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

std::string hex(std::uint64_t value)
{
    char text[19];
    std::snprintf(text, sizeof(text), "0x%016llx", static_cast<unsigned long long>(value));
    return text;
}

float tick_seconds()
{
    std::string error;
    const auto clock = lykoware::FixedStep::create({}, error);
    return clock ? static_cast<float>(clock->tick_seconds()) : 1.F / 60.F;
}

std::unique_ptr<physics::World> make_physics(std::uint32_t workers = 0)
{
    physics::WorldConfig config;
    config.worker_threads = workers;
    std::string error;
    auto world = physics::World::create(config, error);
    if (!world) {
        std::cerr << "FATAL: physics world: " << error << '\n';
        std::exit(EXIT_FAILURE);
    }
    return world;
}

physics::BodyDesc ground_desc()
{
    physics::BodyDesc ground;
    ground.shape = physics::BoxShape{{20.F, 0.5F, 20.F}};
    ground.motion = physics::MotionType::Static;
    ground.position = {0.0, -0.5, 0.0};
    return ground;
}

physics::BodyDesc crate_desc(double x, double y)
{
    physics::BodyDesc crate;
    crate.shape = physics::BoxShape{{0.25F, 0.25F, 0.25F}};
    crate.position = {x, y, 0.0};
    return crate;
}

void test_world_and_link_contracts()
{
    auto a = make_physics(1);
    auto b = make_physics(1);
    std::string error;
    const auto first = a->create_body(crate_desc(0, 2), error);
    const auto foreign = b->create_body(crate_desc(0, 2), error);
    expect(first && foreign && first->index == foreign->index && first->generation == foreign->generation,
           "two worlds exercise matching slots and generations");
    expect(!a->is_valid(*foreign) && !a->state(*foreign) && !a->set_velocity(*foreign, {}, {})
               && !a->destroy_body(*foreign) && a->is_valid(*first), "all body operations reject foreign owners");
    const auto walker_a = a->create_character({}, error);
    const auto walker_b = b->create_character({}, error);
    expect(walker_a && walker_b && !a->is_valid(*walker_b) && !a->character_state(*walker_b)
               && !a->set_character_input(*walker_b, {}) && !a->destroy_character(*walker_b),
           "all character operations reject foreign owners");
    ecs::World entities;
    link::Link link(entities, *a);
    bool refused = false;
    try {
        link::Link duplicate(entities, *b);
    } catch (const std::logic_error&) {
        refused = true;
    }
    expect(refused, "a second physics link cannot replace ownership hooks");
    expect(!entities.on_remove<link::RigidBody>({}) && !entities.on_remove<link::Character>({}),
           "ordinary hook replacement cannot erase subsystem ownership");
    const auto crate = entities.create();
    expect(link.add_body(crate, crate_desc(0, 2), error) && !link.add_character(crate, {}, error),
           "body and character ownership are mutually exclusive");
    const auto walker = entities.create();
    expect(link.add_character(walker, {}, error) && !link.add_body(walker, crate_desc(0, 2), error),
           "character also excludes body ownership");
    refused = false;
    try {
        ecs::World moved(std::move(entities));
        throw std::runtime_error("linked move unexpectedly succeeded");
    } catch (const std::logic_error&) {
        refused = true;
    }
    expect(refused && entities.alive(crate), // NOLINT(bugprone-use-after-move) -- the move must throw before transfer. "linked world move is refused without damaging ownership");
    ecs::World locked;
    const auto e = locked.create();
    locked.add<int>(e, 1);
    refused = false;
    locked.each<int>([&](ecs::Entity, int&) {
        try {
            link::Link invalid(locked, *a);
        } catch (const std::logic_error&) {
            refused = true;
        }
    });
    expect(refused, "link creation while locked fails explicitly");
    link::Link valid_after_refusal(locked, *a);
    expect(valid_after_refusal.add_body(e, crate_desc(0, 2), error), "failed link creation leaves hooks unclaimed");
}

void test_bodies_follow_physics()
{
    auto physics = make_physics();
    ecs::World entities;
    link::Link linked(entities, *physics);
    std::string error;

    const ecs::Entity ground = entities.create();
    expect(linked.add_body(ground, ground_desc(), error), "the ground gets a body");
    const ecs::Entity crate = entities.create();
    expect(linked.add_body(crate, crate_desc(0.0, 3.0), error), "a crate gets a body");
    expect(entities.has<link::RigidBody>(crate) && entities.has<link::Transform>(crate),
           "a body comes with RigidBody and Transform components");
    expect(entities.get<link::Transform>(crate)->position.y == 3.0, "the Transform starts where the body is");
    expect(physics->body_count() == 2, "two physics bodies exist");

    bool stepped = true;
    for (int i = 0; i < 120; ++i) {
        stepped = linked.step(tick_seconds()) && stepped;
    }
    expect(stepped, "every step succeeds");
    const physics::BodyId crate_body = entities.get<link::RigidBody>(crate)->id();
    const auto state = physics->state(crate_body);
    const link::Transform* transform = entities.get<link::Transform>(crate);
    expect(state && transform->position.y == state->position.y && transform->rotation.w == state->rotation.w,
           "after stepping, the Transform equals the physics state");
    expect(std::fabs(transform->position.y - 0.25) < 0.05, "the crate fell and rests on the ground");

    // Refusals change nothing.
    expect(!linked.add_body(crate, crate_desc(0.0, 5.0), error) && !error.empty(), "a second body is refused");
    const ecs::Entity dead = entities.create();
    entities.destroy(dead);
    expect(!linked.add_body(dead, crate_desc(0.0, 5.0), error), "a destroyed entity cannot get a body");
    physics::BodyDesc bad = crate_desc(0.0, 5.0);
    bad.shape = physics::SphereShape{-1.F};
    const ecs::Entity empty = entities.create();
    expect(!linked.add_body(empty, bad, error) && !entities.has<link::RigidBody>(empty)
               && !entities.has<link::Transform>(empty),
           "a body physics refuses adds no components");
    expect(physics->body_count() == 2, "refused bodies create nothing in physics");
    bool refused_in_each = false;
    entities.each<link::RigidBody>([&](ecs::Entity e, link::RigidBody&) {
        refused_in_each = !linked.add_body(e, crate_desc(1.0, 1.0), error);
    });
    expect(refused_in_each && physics->body_count() == 2, "add_body is refused during each");

    // Removing the component, or destroying the entity, destroys the physics body.
    entities.remove<link::RigidBody>(crate);
    expect(!physics->is_valid(crate_body) && physics->body_count() == 1, "removing RigidBody destroys the body");
    expect(entities.alive(crate) && entities.has<link::Transform>(crate), "the entity and its Transform remain");
    const physics::BodyId ground_body = entities.get<link::RigidBody>(ground)->id();
    entities.destroy(ground);
    expect(!physics->is_valid(ground_body) && physics->body_count() == 0, "destroying the entity destroys its body");
}

void test_characters_and_lifetimes()
{
    auto physics = make_physics();
    std::string error;
    {
        ecs::World entities;
        link::Link linked(entities, *physics);
        linked.add_body(entities.create(), ground_desc(), error);
        const ecs::Entity walker = entities.create();
        physics::CharacterDesc desc;
        desc.position = {0.0, 0.0, 0.0};
        expect(linked.add_character(walker, desc, error), "a character can be added to an entity");
        expect(!linked.add_character(walker, desc, error), "a second character is refused");
        const physics::CharacterId walker_id = entities.get<link::Character>(walker)->id();
        physics->set_character_input(walker_id, {2.F, 0.F, 0.F});
        for (int i = 0; i < 60; ++i) {
            linked.step(tick_seconds());
        }
        const link::Transform* transform = entities.get<link::Transform>(walker);
        expect(std::fabs(transform->position.x - 2.0) < 0.1 && std::fabs(transform->position.y) < 0.05,
               "the character's Transform follows it walking 2 m in 1 s");
        expect(transform->rotation.w == 1.F, "a character stays upright");

        entities.remove<link::Character>(walker);
        expect(!physics->is_valid(walker_id) && physics->character_count() == 0,
               "removing Character destroys the physics character");
        linked.add_character(walker, desc, error);
        for (int i = 0; i < 5; ++i) {
            linked.add_body(entities.create(), crate_desc(static_cast<double>(i), 1.0), error);
        }
        expect(physics->body_count() == 6 && physics->character_count() == 1, "six bodies and a character");
    }
    expect(physics->body_count() == 0 && physics->character_count() == 0,
           "destroying the linked ECS world destroys all its physics objects");
}

// A game-style loop: crates rain down and a system despawns any that fall off the edge,
// using a ChangeBuffer because each() refuses destroys.
std::uint64_t despawn_run(std::uint32_t workers, std::size_t& despawned, std::size_t& left)
{
    auto physics = make_physics(workers);
    ecs::World entities;
    link::Link linked(entities, *physics);
    std::string error;
    physics::BodyDesc platform = ground_desc();
    platform.shape = physics::BoxShape{{2.F, 0.5F, 2.F}};
    linked.add_body(entities.create(), platform, error);
    for (int i = 0; i < 24; ++i) {
        // Spread from x = -4.6 to 4.6: the outer crates miss the 4 m platform.
        linked.add_body(entities.create(), crate_desc(-4.6 + 0.4 * static_cast<double>(i), 2.0 + 0.1 * (i % 3)),
                        error);
    }
    ecs::ChangeBuffer changes;
    despawned = 0;
    for (int tick = 0; tick < 180; ++tick) {
        linked.step(tick_seconds());
        entities.each<link::RigidBody, link::Transform>([&](ecs::Entity e, link::RigidBody&, link::Transform& t) {
            if (t.position.y < -3.0) {
                changes.destroy(e);
            }
        });
        despawned += changes.apply(entities);
    }
    left = entities.size();
    expect(physics->body_count() == left, "every despawned entity's body is gone too");
    return physics->state_hash();
}

// The physics state hash after the despawn loop; every build must produce it (ADR 0018).
constexpr std::uint64_t expected_despawn_hash = 0x7d263e8cb7f180dbULL;

void test_despawn_with_change_buffer()
{
    std::size_t despawned = 0;
    std::size_t left = 0;
    const std::uint64_t first = despawn_run(0, despawned, left);
    std::cout << "physics.ecs_link: " << despawned << " crates despawned, " << left
              << " entities left; physics state hash " << hex(first) << '\n';
    expect(despawned > 0 && left > 1, "some crates fell off and were despawned, the rest stayed");
    std::size_t despawned_again = 0;
    std::size_t left_again = 0;
    const std::uint64_t one = despawn_run(1, despawned_again, left_again);
    const std::uint64_t four = despawn_run(4, despawned_again, left_again);
    expect(first == one && one == four && despawned_again == despawned && left_again == left,
           "the same game loop gives the same result every run and with 1 or 4 physics threads");
    expect(first == expected_despawn_hash,
           "the despawn loop's hash matches the value every platform must produce (" + hex(expected_despawn_hash) + ")");
}

}

int main()
{
    test_world_and_link_contracts();
    test_bodies_follow_physics();
    test_characters_and_lifetimes();
    test_despawn_with_change_buffer();
    if (g_failures != 0) {
        std::cerr << g_failures << " physics-ECS assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "physics ecs link passed\n";
    return EXIT_SUCCESS;
}
