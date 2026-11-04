#include <lykoware/ecs.hpp>

#include <atomic>
#include <utility>

namespace lykoware::ecs {

namespace detail {

TypeId next_type_id()
{
    static std::atomic<TypeId> next{0};
    return next.fetch_add(1, std::memory_order_relaxed);
}

}

World::World() = default;

World::~World()
{
    destroy_all();
}

World::World(World&& other) noexcept
    : generations_(std::exchange(other.generations_, {}))
    , alive_(std::exchange(other.alive_, {}))
    , free_(std::exchange(other.free_, {}))
    , pools_(std::exchange(other.pools_, {}))
    , live_(std::exchange(other.live_, 0))
    , iterating_(std::exchange(other.iterating_, 0))
{
}

World& World::operator=(World&& other) noexcept
{
    if (this != &other) {
        destroy_all();
        generations_ = std::exchange(other.generations_, {});
        alive_ = std::exchange(other.alive_, {});
        free_ = std::exchange(other.free_, {});
        pools_ = std::exchange(other.pools_, {});
        live_ = std::exchange(other.live_, 0);
        iterating_ = std::exchange(other.iterating_, 0);
    }
    return *this;
}

void World::destroy_all()
{
    iterating_ = 0;
    for (std::uint32_t index = 0; index < generations_.size(); ++index) {
        if (alive_[index] != 0) {
            destroy(Entity{index, generations_[index]});
        }
    }
}

Entity World::create()
{
    if (iterating_ != 0) {
        return {};
    }
    std::uint32_t index = 0;
    if (!free_.empty()) {
        // Last freed, first reused: a deterministic order.
        index = free_.back();
        free_.pop_back();
    } else {
        if (generations_.size() >= UINT32_MAX) {
            return {};
        }
        index = static_cast<std::uint32_t>(generations_.size());
        generations_.push_back(0);
        alive_.push_back(0);
    }
    // Generation 0 marks the default handle, so a slot's first entity gets generation 1.
    ++generations_[index];
    alive_[index] = 1;
    ++live_;
    return {index, generations_[index]};
}

bool World::destroy(Entity entity)
{
    if (iterating_ != 0 || !alive(entity)) {
        return false;
    }
    {
        // Hooks run with the world locked, so they cannot change it under this loop.
        const IterationGuard guard(iterating_);
        for (const std::unique_ptr<detail::PoolBase>& pool : pools_) {
            if (pool) {
                pool->remove(entity);
            }
        }
    }
    alive_[entity.index] = 0;
    --live_;
    // A slot whose generation would wrap round is retired rather than reused, so an old handle
    // can never match a later entity.
    if (generations_[entity.index] != UINT32_MAX) {
        free_.push_back(entity.index);
    }
    return true;
}

std::size_t ChangeBuffer::apply(World& world)
{
    if (world.locked()) {
        return 0;
    }
    std::size_t applied = 0;
    // Take the changes first: a setup function may record more changes into this buffer,
    // which then wait for the next apply().
    std::vector<std::unique_ptr<Change>> changes = std::move(changes_);
    changes_.clear();
    for (const std::unique_ptr<Change>& change : changes) {
        applied += change->apply(world) ? 1U : 0U;
    }
    return applied;
}

bool World::alive(Entity entity) const
{
    return entity.generation != 0 && entity.index < generations_.size() && alive_[entity.index] != 0
        && generations_[entity.index] == entity.generation;
}

}
