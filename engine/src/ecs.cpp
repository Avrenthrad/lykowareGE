#include <lykoware/ecs.hpp>

#include <atomic>

namespace lykoware::ecs {

namespace detail {

TypeId next_type_id()
{
    static std::atomic<TypeId> next{0};
    return next.fetch_add(1, std::memory_order_relaxed);
}

}

World::World() = default;
World::~World() = default;
World::World(World&&) noexcept = default;
World& World::operator=(World&&) noexcept = default;

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
    for (const std::unique_ptr<detail::PoolBase>& pool : pools_) {
        if (pool) {
            pool->remove(entity.index);
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

bool World::alive(Entity entity) const
{
    return entity.generation != 0 && entity.index < generations_.size() && alive_[entity.index] != 0
        && generations_[entity.index] == entity.generation;
}

}
