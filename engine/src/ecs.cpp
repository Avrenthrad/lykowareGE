#include <lykoware/ecs.hpp>
#include "world_identity.hpp"

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

World::World() : owner_(lykoware::detail::next_world_identity()) {}

World::~World()
{
    destroy_all();
}

bool World::has_owned_hooks() const
{
    for (const auto& pool : pools_) {
        if (pool && pool->owned_hook()) {
            return true;
        }
    }
    return false;
}

World::World(World&& other)
{
    *this = std::move(other);
}

World& World::operator=(World&& other)
{
    if (this != &other) {
        if (locked() || other.locked() || has_owned_hooks() || other.has_owned_hooks()) {
            throw std::logic_error("cannot move a locked or subsystem-linked ECS world");
        }
        destroy_all();
        owner_ = std::exchange(other.owner_, 0);
        generations_.clear();
        alive_.clear();
        free_.clear();
        removal_order_.clear();
        pools_.clear();
        // Swap empty storage rather than constructing vector temporaries. No allocation,
        // including MSVC Debug iterator proxies, can interrupt the ownership transfer.
        generations_.swap(other.generations_);
        alive_.swap(other.alive_);
        free_.swap(other.free_);
        pools_.swap(other.pools_);
        removal_order_.swap(other.removal_order_);
        live_ = std::exchange(other.live_, 0);
    }
    return *this;
}

void World::destroy_all()
{
    iterating_ = 0;
    for (std::uint32_t index = 0; index < generations_.size(); ++index) {
        if (alive_[index] != 0) {
            destroy(Entity{index, generations_[index], owner_});
        }
    }
}

Entity World::create()
{
    if (iterating_ != 0) {
        return {};
    }
    if (owner_ == 0) {
        owner_ = lykoware::detail::next_world_identity();
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
        const std::size_t required = generations_.size() + 1;
        const std::size_t capacity = required * 2;
        if (generations_.capacity() < required) {
            generations_.reserve(capacity);
        }
        if (alive_.capacity() < required) {
            alive_.reserve(capacity);
        }
        if (free_.capacity() < required) {
            free_.reserve(capacity);
        }
        generations_.push_back(0);
        alive_.push_back(0);
    }
    // Generation 0 marks the default handle, so a slot's first entity gets generation 1.
    ++generations_[index];
    alive_[index] = 1;
    ++live_;
    return {index, generations_[index], owner_};
}

bool World::destroy(Entity entity)
{
    if (iterating_ != 0 || !alive(entity)) {
        return false;
    }
    {
        // Hooks run with the world locked, so they cannot change it under this loop.
        const IterationGuard guard(iterating_);
        for (detail::PoolBase* pool : removal_order_) {
            pool->remove(entity);
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
    std::list<std::unique_ptr<Change>> changes;
    changes.splice(changes.end(), changes_);
    while (!changes.empty()) {
        auto change = std::move(changes.front());
        changes.pop_front();
        try {
            applied += change->apply(world) ? 1U : 0U;
        } catch (...) {
            changes_.splice(changes_.begin(), changes);
            throw;
        }
    }
    return applied;
}

bool World::alive(Entity entity) const
{
    return entity.owner != 0 && entity.owner == owner_ && entity.generation != 0 && entity.index < generations_.size() && alive_[entity.index] != 0
        && generations_[entity.index] == entity.generation;
}

}
