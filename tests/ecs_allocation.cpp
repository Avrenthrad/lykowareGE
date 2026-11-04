// Fail each C++ allocation in turn; no production injection hooks are needed.
#include <lykoware/ecs.hpp>
#include <lykoware/physics.hpp>
#include <lykoware/physics_ecs.hpp>

#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>

#ifdef _MSC_VER
#include <crtdbg.h>
#endif

namespace {
thread_local int fail_after = -1;
}

void* operator new(std::size_t size)
{
    if (fail_after >= 0 && fail_after-- == 0) {
        throw std::bad_alloc();
    }
    if (void* allocation = std::malloc(size == 0 ? 1 : size)) {
        return allocation;
    }
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* allocation) noexcept { std::free(allocation); }
void operator delete[](void* allocation) noexcept { std::free(allocation); }
void operator delete(void* allocation, std::size_t) noexcept { std::free(allocation); }
void operator delete[](void* allocation, std::size_t) noexcept { std::free(allocation); }

namespace {
namespace ecs = lykoware::ecs;
namespace physics = lykoware::physics;
namespace linked = lykoware::physics_ecs;

void require(bool condition)
{
    if (!condition) {
        throw std::runtime_error("allocation failure corrupted runtime state");
    }
}

struct Component {
    int value = 0;
    explicit Component(int initial) : value(initial)
    {
        if (initial < 0) {
            throw std::runtime_error("constructor failure");
        }
    }
    Component(Component&&) noexcept = default;
    Component& operator=(Component&&) noexcept = default;
};

void entity_and_component_failures()
{
    for (int failure = 0; failure < 15; ++failure) {
        ecs::World world;
        const auto first = world.create();
        world.add<Component>(first, 7);
        bool created = false;
        fail_after = failure;
        try {
            const auto added = world.create();
            created = true;
            world.add<Component>(added, 9);
        } catch (const std::bad_alloc&) {
        }
        fail_after = -1;
        require(world.alive(first) && world.get<Component>(first)->value == 7);
        require(world.size() == (created ? 2U : 1U));
        std::size_t seen = 0;
        world.each<Component>([&](ecs::Entity entity, Component& value) {
            require(world.alive(entity) && (value.value == 7 || value.value == 9));
            ++seen;
        });
        require(seen == world.count<Component>());
        try {
            world.add<Component>(world.create(), -1);
        } catch (const std::runtime_error&) {
        }
        require(seen == world.count<Component>());
        const auto recovery = world.create();
        require(world.add<Component>(recovery, 11) != nullptr);
        // Destruction must be allocation-free, even when slot storage has grown.
        fail_after = 0;
        require(world.destroy(first) && world.destroy(recovery));
        fail_after = -1;
    }
}

void attachment_failures()
{
    std::string error;
    physics::WorldConfig config;
    config.worker_threads = 1;
    auto physical = physics::World::create(config, error);
    require(static_cast<bool>(physical));
    for (bool character : {false, true}) {
        for (bool existing : {false, true}) {
            bool completed = false;
            for (int failure = 0; failure < 48 && !completed; ++failure) {
                std::cerr << "attach character=" << character << " existing=" << existing << " fail=" << failure << std::endl;
                ecs::World world;
                linked::Link link(world, *physical);
                const auto entity = world.create();
                if (existing) {
                    world.add<linked::Transform>(entity, linked::Transform{{10, 20, 30}, {}});
                }
                fail_after = failure;
                try {
                    completed = character ? link.add_character(entity, {}, error) : link.add_body(entity, {}, error);
                } catch (const std::bad_alloc&) {
                }
                fail_after = -1;
                if (!completed) {
                    require(physical->body_count() == 0 && physical->character_count() == 0);
                    require(!world.has<linked::RigidBody>(entity) && !world.has<linked::Character>(entity));
                    require(world.has<linked::Transform>(entity) == existing);
                    if (existing) {
                        require(world.get<linked::Transform>(entity)->position.x == 10);
                    }
                }
            }
            require(completed && physical->body_count() == 0 && physical->character_count() == 0);
        }
    }
}
}

int main()
{
#if defined(_MSC_VER) && defined(_DEBUG)
    // Debug CRT assertions must report into CI logs rather than open an invisible dialog.
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    std::cerr << "entity allocation failures" << std::endl;
    entity_and_component_failures();
    std::cerr << "physics attachment allocation failures" << std::endl;
    attachment_failures();
    std::cout << "allocation failure recovery passed\n";
}
