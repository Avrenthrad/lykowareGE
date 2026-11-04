// ecs.world: Lykoware's entity-component system (ADR 0023). Links only lykoware::engine.

#include <lykoware/ecs.hpp>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace ecs = lykoware::ecs;

int g_failures = 0;

void expect(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

struct Position {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct Velocity {
    float x = 0.F;
    float y = 0.F;
    float z = 0.F;
};

struct Health {
    int value = 100;
};

// Counts live instances, to show components are destroyed exactly once.
struct Tracked {
    static inline int live = 0;
    std::string name;

    explicit Tracked(std::string text) : name(std::move(text)) { ++live; }
    Tracked(const Tracked& other) : name(other.name) { ++live; }
    Tracked(Tracked&& other) noexcept : name(std::move(other.name)) { ++live; }
    Tracked& operator=(const Tracked&) = default;
    Tracked& operator=(Tracked&&) noexcept = default;
    ~Tracked() { --live; }
};

void test_handles()
{
    ecs::World world;
    expect(!world.alive(ecs::Entity{}), "the default handle is never valid");
    expect(world.size() == 0, "a new world is empty");

    const ecs::Entity a = world.create();
    const ecs::Entity b = world.create();
    expect(world.alive(a) && world.alive(b) && !(a == b), "created entities are alive and distinct");
    expect(a.generation != 0 && b.generation != 0, "live handles never have generation 0");
    expect(world.size() == 2, "size counts live entities");

    expect(world.destroy(a), "an entity can be destroyed");
    expect(!world.alive(a), "a destroyed entity is not alive");
    expect(!world.destroy(a), "destroying twice fails");
    expect(world.size() == 1, "size drops after destroy");

    const ecs::Entity c = world.create();
    expect(c.index == a.index && c.generation != a.generation, "a freed slot is reused with a new generation");
    expect(!world.alive(a) && world.alive(c), "the old handle never reaches the new entity");
    expect(!world.alive(ecs::Entity{999, 1, b.owner}), "an out-of-range handle is not alive");
    expect(!world.alive(ecs::Entity{b.index, b.generation + 1, b.owner}), "a wrong generation is not alive");

    // Slots are reused last freed, first used.
    std::vector<ecs::Entity> many;
    for (int i = 0; i < 5; ++i) {
        many.push_back(world.create());
    }
    world.destroy(many[1]);
    world.destroy(many[3]);
    expect(world.create().index == many[3].index, "the last freed slot is reused first");
    expect(world.create().index == many[1].index, "then the one freed before it");
}

void test_components()
{
    ecs::World world;
    const ecs::Entity e = world.create();
    Position* position = world.add<Position>(e, Position{1.0, 2.0, 3.0});
    expect(position != nullptr && position->y == 2.0, "add returns the new component");
    expect(world.has<Position>(e) && !world.has<Velocity>(e), "has reports which components an entity has");
    expect(world.add<Position>(e) == nullptr, "a second component of the same type is refused");
    expect(world.get<Position>(e)->z == 3.0, "get finds the component");
    world.get<Position>(e)->z = 4.0;
    const ecs::World& view = world;
    expect(view.get<Position>(e)->z == 4.0, "components can be changed and read through a const world");
    expect(world.get<Velocity>(e) == nullptr, "get of a missing component is nullptr");
    expect(world.count<Position>() == 1 && world.count<Velocity>() == 0, "count counts components of a type");

    expect(world.remove<Position>(e), "a component can be removed");
    expect(!world.has<Position>(e) && world.alive(e), "removing a component keeps the entity");
    expect(!world.remove<Position>(e), "removing a missing component fails");
    expect(!world.remove<Velocity>(e), "removing a never-used component type fails");

    const ecs::Entity dead = world.create();
    world.destroy(dead);
    expect(world.add<Health>(dead) == nullptr, "a destroyed entity cannot get components");
    expect(world.add<Health>(ecs::Entity{}) == nullptr, "the default handle cannot get components");

    // A stale handle to a reused slot never sees the new entity's components.
    const ecs::Entity old = world.create();
    world.add<Health>(old, Health{7});
    world.destroy(old);
    const ecs::Entity reused = world.create();
    world.add<Health>(reused, Health{9});
    expect(reused.index == old.index, "the slot was reused");
    expect(world.get<Health>(old) == nullptr && !world.remove<Health>(old), "a stale handle sees nothing");
    expect(world.get<Health>(reused)->value == 9, "the new entity has its own component");
}

void test_destroy_and_lifetime()
{
    {
        ecs::World world;
        const ecs::Entity a = world.create();
        const ecs::Entity b = world.create();
        world.add<Tracked>(a, std::string("a"));
        world.add<Tracked>(b, std::string("b"));
        world.add<Health>(a);
        expect(Tracked::live == 2, "two components are alive");
        world.destroy(a);
        expect(Tracked::live == 1 && world.count<Health>() == 0, "destroying an entity destroys all its components");
        expect(world.get<Tracked>(b)->name == "b", "other entities keep theirs");

        ecs::World moved = std::move(world);
        expect(moved.alive(b) && moved.get<Tracked>(b)->name == "b", "a moved world keeps its entities");
    }
    expect(Tracked::live == 0, "destroying the world destroys every component exactly once");
}

// Removing from the middle moves the last component into the hole; every entity must still
// find its own value.
void test_packed_storage()
{
    ecs::World world;
    std::vector<ecs::Entity> entities;
    for (int i = 0; i < 6; ++i) {
        entities.push_back(world.create());
        world.add<Health>(entities.back(), Health{i});
        world.add<Tracked>(entities.back(), "entity " + std::to_string(i));
    }
    world.remove<Health>(entities[1]);
    world.remove<Tracked>(entities[1]);
    world.destroy(entities[3]);
    world.remove<Health>(entities[5]);

    bool values_match = true;
    for (int i : {0, 2, 4}) {
        const auto index = static_cast<std::size_t>(i);
        values_match = values_match && world.get<Health>(entities[index])->value == i
                    && world.get<Tracked>(entities[index])->name == "entity " + std::to_string(i);
    }
    expect(values_match, "after removals every entity still finds its own components");
    expect(world.get<Tracked>(entities[5])->name == "entity 5", "a component of another type is untouched");
    expect(world.count<Health>() == 3 && world.count<Tracked>() == 4, "counts follow removals");

    int visited = 0;
    int sum = 0;
    world.each<Health>([&](ecs::Entity, Health& health) {
        ++visited;
        sum += health.value;
    });
    expect(visited == 3 && sum == 0 + 2 + 4, "iteration visits exactly the remaining components");
}

void test_each()
{
    ecs::World world;
    std::vector<ecs::Entity> movers;
    for (int i = 0; i < 4; ++i) {
        const ecs::Entity e = world.create();
        world.add<Position>(e, Position{static_cast<double>(i), 0.0, 0.0});
        if (i % 2 == 0) {
            world.add<Velocity>(e, Velocity{1.F, 0.F, 0.F});
            movers.push_back(e);
        }
    }
    const ecs::Entity only_velocity = world.create();
    world.add<Velocity>(only_velocity, Velocity{5.F, 0.F, 0.F});

    std::vector<ecs::Entity> seen;
    world.each<Position, Velocity>([&](ecs::Entity e, Position& p, Velocity& v) {
        p.x += static_cast<double>(v.x);
        seen.push_back(e);
    });
    expect(seen == movers, "each visits the entities with every listed component, in storage order");
    expect(world.get<Position>(movers[1])->x == 3.0 && world.get<Position>(only_velocity) == nullptr,
           "each can change component values");

    // Three components: only the entity with all three is visited.
    world.add<Health>(movers[0], Health{42});
    world.add<Health>(world.create(), Health{1});
    std::vector<ecs::Entity> all_three;
    world.each<Position, Velocity, Health>([&](ecs::Entity e, Position&, Velocity&, Health& h) {
        all_three.push_back(e);
        h.value += 1;
    });
    expect(all_three.size() == 1 && all_three[0] == movers[0] && world.get<Health>(movers[0])->value == 43,
           "each with three components visits only entities that have all three");
    world.remove<Health>(movers[0]);

    struct Unused {};
    int count = 0;
    world.each<Unused>([&](ecs::Entity, Unused&) { ++count; });
    world.each<Position, Unused>([&](ecs::Entity, Position&, Unused&) { ++count; });
    world.each<Velocity, Health>([&](ecs::Entity, Velocity&, Health&) { ++count; });
    expect(count == 0, "each over a combination nobody has visits nothing");
}

void test_changes_during_each()
{
    ecs::World world;
    const ecs::Entity a = world.create();
    const ecs::Entity b = world.create();
    world.add<Health>(a);
    world.add<Health>(b);

    bool refused = true;
    world.each<Health>([&](ecs::Entity e, Health&) {
        refused = refused && !world.alive(world.create());
        refused = refused && !world.destroy(e);
        refused = refused && world.add<Velocity>(e) == nullptr;
        refused = refused && !world.remove<Health>(e);
        // Reading and nested iteration are allowed.
        int inner = 0;
        world.each<Health>([&](ecs::Entity, Health&) { ++inner; });
        refused = refused && inner == 2 && world.get<Health>(e) != nullptr;
    });
    expect(refused, "structural changes are refused during each, reads are allowed");
    expect(world.size() == 2 && world.count<Health>() == 2 && world.count<Velocity>() == 0,
           "nothing changed during each");

    try {
        world.each<Health>([](ecs::Entity, Health&) { throw std::runtime_error("system failed"); });
    } catch (const std::runtime_error&) {
    }
    expect(world.alive(world.create()), "changes are allowed again after each exits by an exception");
}

// The same calls give the same handles, values and iteration order.
std::vector<std::uint64_t> scripted_run()
{
    ecs::World world;
    std::vector<ecs::Entity> entities;
    std::uint32_t seed = 12345;
    auto next = [&seed]() {
        seed = seed * 1664525U + 1013904223U;
        return seed >> 8U;
    };
    for (int step = 0; step < 5000; ++step) {
        const std::uint32_t action = next() % 6;
        if (action <= 1 || entities.empty()) {
            const ecs::Entity e = world.create();
            entities.push_back(e);
            world.add<Health>(e, Health{static_cast<int>(next() % 1000)});
            if (next() % 2 == 0) {
                world.add<Velocity>(e, Velocity{static_cast<float>(next() % 10), 0.F, 0.F});
            }
        } else {
            const std::size_t pick = next() % entities.size();
            const ecs::Entity e = entities[pick];
            if (action == 2) {
                world.destroy(e);
                entities[pick] = entities.back();
                entities.pop_back();
            } else if (action == 3) {
                world.remove<Health>(e);
            } else if (action == 4) {
                world.add<Health>(e, Health{static_cast<int>(next() % 1000)});
            } else {
                world.remove<Velocity>(e);
            }
        }
    }
    std::vector<std::uint64_t> trace;
    world.each<Health, Velocity>([&](ecs::Entity e, Health& h, Velocity& v) {
        trace.push_back((std::uint64_t{e.index} << 32U) | e.generation);
        trace.push_back(static_cast<std::uint64_t>(h.value) * 100U + static_cast<std::uint64_t>(v.x));
    });
    trace.push_back(world.size());
    return trace;
}

void test_determinism()
{
    const std::vector<std::uint64_t> first = scripted_run();
    const std::vector<std::uint64_t> second = scripted_run();
    expect(first.size() > 100, "the scripted run leaves many entities with both components");
    expect(first == second, "the same calls give the same handles, values and iteration order");
}

void test_remove_hooks()
{
    std::vector<std::string> removed;
    ecs::Entity a;
    ecs::Entity b;
    {
        ecs::World world;
        world.on_remove<Health>([&](ecs::Entity e, Health& h) {
            removed.push_back("health " + std::to_string(e.index) + " " + std::to_string(h.value));
            // Structural changes are refused inside a hook.
            const bool refused = !world.alive(world.create()) && !world.destroy(e) && world.add<Velocity>(e) == nullptr
                              && !world.remove<Health>(e) && world.locked();
            if (!refused) {
                removed.emplace_back("a hook changed the world");
            }
        });
        a = world.create();
        b = world.create();
        const ecs::Entity c = world.create();
        world.add<Health>(a, Health{1});
        world.add<Health>(b, Health{2});
        world.add<Health>(c, Health{3});
        world.add<Position>(c);

        world.remove<Health>(a);
        expect(removed.size() == 1 && removed[0] == "health 0 1", "remove runs the hook with the component");
        world.destroy(c);
        expect(removed.size() == 2 && removed[1] == "health 2 3", "destroy runs the hook");
        expect(!world.remove<Health>(a) && removed.size() == 2, "nothing removed, no hook");
        expect(!world.locked(), "the world is unlocked after hooks");

        world.on_remove<Position>({});
        expect(world.on_remove<Health>([&](ecs::Entity e, Health&) { removed.push_back("new " + std::to_string(e.index)); }),
               "a hook can be replaced");
        ecs::World other;
        const ecs::Entity d = other.create();
        other.add<Health>(d, Health{4});
        world = std::move(other);
        expect(removed.size() == 3 && removed[2] == "new 1", "move-assigning over a world runs its hooks");
        expect(world.alive(d) && world.get<Health>(d)->value == 4, "the moved-in world keeps its entities");
    }
    expect(removed.size() == 3, "the moved-in world had no hooks, so its destruction runs none");

    std::vector<int> order;
    {
        ecs::World world;
        world.on_remove<Health>([&](ecs::Entity, Health& h) { order.push_back(h.value); });
        ecs::Entity second;
        for (int i = 0; i < 4; ++i) {
            const auto entity = world.create();
            world.add<Health>(entity, Health{i});
            if (i == 1) {
                second = entity;
            }
        }
        world.destroy(second);
    }
    expect(order == std::vector<int>{1, 0, 2, 3}, "destroying the world runs the hooks of the rest, in slot order");

    ecs::World world;
    bool called = false;
    world.on_remove<Health>([&](ecs::Entity, Health&) { called = true; });
    world.on_remove<Health>({});
    const auto cleared = world.create();
    world.add<Health>(cleared);
    world.destroy(cleared);
    expect(!called, "an empty hook clears it");
    world.add<Health>(world.create());
    bool refused_inside_each = false;
    world.each<Health>([&](ecs::Entity, Health&) { refused_inside_each = !world.on_remove<Health>({}); });
    expect(refused_inside_each, "hooks cannot be changed during each");
}

void test_change_buffer()
{
    ecs::World world;
    std::vector<ecs::Entity> entities;
    for (int i = 0; i < 6; ++i) {
        entities.push_back(world.create());
        world.add<Health>(entities.back(), Health{i * 10});
    }

    // A system that removes the dying and spawns a replacement for each, during iteration.
    ecs::ChangeBuffer changes;
    world.each<Health>([&](ecs::Entity e, Health& h) {
        if (h.value < 30) {
            changes.destroy(e);
            changes.create([value = h.value](ecs::World& w, ecs::Entity spawned) {
                w.add<Health>(spawned, Health{100 + value});
            });
        } else {
            changes.add<Velocity>(e, Velocity{1.F, 0.F, 0.F});
        }
    });
    expect(changes.size() == 9 && world.size() == 6, "changes are recorded, not applied, during each");
    expect(changes.apply(world) == 9 && changes.empty(), "apply makes every change and empties the buffer");
    expect(world.size() == 6 && world.count<Velocity>() == 3, "three replaced, three given velocity");
    expect(!world.alive(entities[0]) && world.get<Velocity>(entities[4]) != nullptr, "the right entities changed");

    // Slots freed by the destroys are reused by the creates in order: 0, 1, 2 freed; 2, 1, 0 reused.
    int spawned_sum = 0;
    world.each<Health>([&](ecs::Entity, Health& h) { spawned_sum += h.value >= 100 ? h.value : 0; });
    expect(spawned_sum == 100 + 110 + 120, "the spawned entities got their components");

    // Changes to an entity destroyed earlier in the buffer do nothing and are not counted.
    changes.destroy(entities[3]);
    changes.add<Position>(entities[3], Position{});
    changes.remove<Health>(entities[4]);
    changes.remove<Health>(entities[4]);
    expect(changes.apply(world) == 2, "changes the world refuses are skipped");

    // Inside each, apply does nothing and keeps the buffer.
    changes.destroy(entities[5]);
    std::size_t applied_inside = 99;
    world.each<Health>([&](ecs::Entity, Health&) { applied_inside = changes.apply(world); });
    expect(applied_inside == 0 && changes.size() == 1 && world.alive(entities[5]), "apply refuses while locked");
    expect(changes.apply(world) == 1 && !world.alive(entities[5]), "and works afterwards");

    // Move-only components can be recorded.
    struct Owned {
        std::unique_ptr<int> value;
    };
    const ecs::Entity holder = world.create();
    changes.add<Owned>(holder, Owned{std::make_unique<int>(7)});
    changes.apply(world);
    expect(world.get<Owned>(holder) != nullptr && *world.get<Owned>(holder)->value == 7, "move-only components work");

    changes.destroy(holder);
    changes.clear();
    expect(changes.apply(world) == 0 && world.alive(holder), "clear drops recorded changes");
}

void test_world_isolation_and_failures()
{
    ecs::World a;
    ecs::World b;
    const auto first = a.create();
    const auto foreign = b.create();
    a.add<Health>(first);
    b.add<Health>(foreign);
    expect(first.index == foreign.index && first.generation == foreign.generation && first != foreign,
           "identical slots in separate worlds have different handles");
    expect(!a.alive(foreign) && !a.destroy(foreign) && !a.remove<Health>(foreign)
               && a.get<Health>(foreign) == nullptr && a.add<Position>(foreign) == nullptr,
           "all entity entry points reject foreign handles");
    ecs::World moved(std::move(a));
    const auto replacement = a.create();
    expect(moved.alive(first) && !a.alive(first) && !moved.alive(replacement),
           "move transfers identity and moved-from reuse gets fresh identity");
    bool refused = false;
    moved.each<Health>([&](ecs::Entity, Health&) {
        try {
            ecs::World invalid(std::move(moved));
        } catch (const std::logic_error&) {
            refused = true;
        }
    });
    expect(refused && !moved.locked() && moved.alive(first), "locked move is refused without corrupting iteration");

    ecs::ChangeBuffer changes;
    changes.create([&](ecs::World& world, ecs::Entity e) {
        world.add<Position>(e);
        changes.add<Position>(first, Position{3, 0, 0});
        if (world.has<Position>(e)) {
            throw std::runtime_error("setup failed");
        }
    });
    changes.add<Health>(first, Health{22});
    try {
        changes.apply(moved);
    } catch (const std::runtime_error&) {
    }
    expect(moved.size() == 1 && moved.count<Position>() == 0 && changes.size() == 2,
           "throwing setup rolls back entity and preserves later and appended changes");
    moved.remove<Health>(first);
    expect(changes.apply(moved) == 2 && moved.get<Health>(first)->value == 22
               && moved.get<Position>(first)->x == 3,
           "preserved commands apply in order on retry");
}

void test_many()
{
    ecs::World world;
    std::vector<ecs::Entity> entities;
    for (int round = 0; round < 3; ++round) {
        for (int i = 0; i < 20000; ++i) {
            const ecs::Entity e = world.create();
            world.add<Position>(e);
            entities.push_back(e);
        }
        for (const ecs::Entity e : entities) {
            world.destroy(e);
        }
        entities.clear();
    }
    expect(world.size() == 0 && world.count<Position>() == 0, "repeated create and destroy rounds leave nothing");
    expect(world.create().index < 20000, "slots are reused rather than growing without bound");
}

}

int main()
{
    test_handles();
    test_components();
    test_destroy_and_lifetime();
    test_packed_storage();
    test_each();
    test_changes_during_each();
    test_determinism();
    test_remove_hooks();
    test_change_buffer();
    test_many();
    test_world_isolation_and_failures();
    if (g_failures != 0) {
        std::cerr << g_failures << " ECS assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "ecs world passed\n";
    return EXIT_SUCCESS;
}
