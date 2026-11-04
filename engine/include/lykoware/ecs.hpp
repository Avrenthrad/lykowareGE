#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

// The Lykoware entity-component system (ADR 0023). Lykoware's own code, no third-party library.
//
// An entity is a generation-checked handle. Components are plain C++ types stored per type in
// packed arrays (sparse sets), so iterating one component type walks contiguous memory.
//
// Determinism: the same calls in the same order give the same slots/generations and iteration
// order. Opaque owner tokens isolate worlds and must not be persisted or hashed. This is
// what replays, saves and later co-op build on (ADR 0021).

namespace lykoware::ecs {

// Handle to an entity. A destroyed entity's handle stays invalid for the rest of the world's
// life: its slot can be reused, but with a new generation, so an old handle never reaches a new
// entity. The default handle is never valid.
struct Entity {
    std::uint32_t index = 0;
    std::uint32_t generation = 0;
    std::uint64_t owner = 0;

    friend bool operator==(const Entity&, const Entity&) = default;
};

namespace detail {

using TypeId = std::uint32_t;

// A small number for each component type, given in order of first use in the process. Used
// only to find a type's storage, never for ordering results.
TypeId next_type_id();

template <class T>
TypeId type_id()
{
    static_assert(std::is_same_v<T, std::remove_cvref_t<T>>, "use the plain component type, not const or a reference");
    static const TypeId id = next_type_id();
    return id;
}

class PoolBase {
public:
    PoolBase() = default;
    PoolBase(const PoolBase&) = delete;
    PoolBase& operator=(const PoolBase&) = delete;
    PoolBase(PoolBase&&) = delete;
    PoolBase& operator=(PoolBase&&) = delete;
    virtual ~PoolBase() = default;
    // Removes the entity's component, if it has one, after calling the removal hook.
    virtual bool remove(Entity entity) = 0;
    [[nodiscard]] virtual bool owned_hook() const = 0;
};

// Components of one type: `values_[i]` belongs to the entity in slot `dense_[i]`, and
// `sparse_[slot]` is that position (or `absent`). Removal moves the last component into the
// hole, so positions change only on removal, and identically for identical calls.
template <class T>
class Pool final : public PoolBase {
public:
    static constexpr std::uint32_t absent = UINT32_MAX;

    [[nodiscard]] T* find(std::uint32_t index)
    {
        return index < sparse_.size() && sparse_[index] != absent ? &values_[sparse_[index]] : nullptr;
    }
    [[nodiscard]] const T* find(std::uint32_t index) const
    {
        return index < sparse_.size() && sparse_[index] != absent ? &values_[sparse_[index]] : nullptr;
    }

    template <class... Args>
    T& emplace(std::uint32_t index, Args&&... args)
    {
        // Construct before reserve: args may refer to an existing component.
        T staged(std::forward<Args>(args)...);
        const std::size_t required = dense_.size() + 1;
        if (required > dense_.capacity()) {
            dense_.reserve(required * 2);
        }
        if (required > values_.capacity()) {
            values_.reserve(required * 2);
        }
        if (index >= sparse_.size()) {
            sparse_.resize(static_cast<std::size_t>(index) + 1, absent);
        }
        values_.push_back(std::move(staged));
        sparse_[index] = static_cast<std::uint32_t>(dense_.size());
        dense_.push_back(index);
        return values_.back();
    }

    bool remove(Entity entity) override
    {
        const std::uint32_t index = entity.index;
        if (index >= sparse_.size() || sparse_[index] == absent) {
            return false;
        }
        if (on_remove_) {
            on_remove_(entity, values_[sparse_[index]]);
        }
        const std::uint32_t position = sparse_[index];
        const std::uint32_t last = static_cast<std::uint32_t>(dense_.size() - 1);
        if (position != last) {
            values_[position] = std::move(values_[last]);
            dense_[position] = dense_[last];
            sparse_[dense_[position]] = position;
        }
        values_.pop_back();
        dense_.pop_back();
        sparse_[index] = absent;
        return true;
    }

    [[nodiscard]] std::size_t size() const { return dense_.size(); }
    [[nodiscard]] std::uint32_t slot_at(std::size_t position) const { return dense_[position]; }
    [[nodiscard]] T& value_at(std::size_t position) { return values_[position]; }
    [[nodiscard]] const T& value_at(std::size_t position) const { return values_[position]; }
    [[nodiscard]] bool owned_hook() const override { return static_cast<bool>(hook_owner_); }
    [[nodiscard]] bool has_hook() const { return static_cast<bool>(on_remove_); }
    void set_on_remove(std::function<void(Entity, T&)> hook) { on_remove_ = std::move(hook); }
    void claim_hook(const std::shared_ptr<void>& owner, std::function<void(Entity, T&)> hook) noexcept
    {
        hook_owner_ = owner;
        on_remove_ = std::move(hook);
    }

private:
    std::shared_ptr<void> hook_owner_;
    std::function<void(Entity, T&)> on_remove_;
    std::vector<std::uint32_t> sparse_;
    std::vector<std::uint32_t> dense_;
    std::vector<T> values_;
};

}

// One set of entities and their components.
//
// Components: nonthrowing move-constructible, move-assignable and destructible types, with no
// const or reference members. An entity has at most one component of each type.
//
// Pointers returned by add() and get() stay valid until the next add() or remove() of that
// component type, or the entity's destruction.
//
// Iteration: each<A, B...>(fn) visits every entity that has all the listed components, in the
// order its A component sits in storage. While any each() is running, structural changes
// (create, destroy, add, remove) are refused; reading and changing component values is fine.
// Record changes in a ChangeBuffer and apply it afterwards instead.
//
// Removal hooks: on_remove<T>(hook) calls hook(entity, component) just before a T is removed,
// whether by remove<T>(), by destroy() or by the world's destruction or move-assignment. For
// one entity, hooks run in pool-registration order within this world. A
// hook may not make structural changes to this world (they are refused) and must not throw.
//
// Threading: one thread at a time. Ownership: the world owns all components; destroying the
// world destroys them. Copy is not provided; move is refused while locked or with owned hooks.
//
// Failure: functions that take an Entity return false or nullptr for an invalid, destroyed or
// foreign handle, and for refused structural changes. Nothing throws for those; std::bad_alloc
// and exceptions from component constructors can propagate.
class World {
public:
    World();
    ~World();
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World(World&&);
    World& operator=(World&&);

    // A new entity with no components. Default (invalid) handle if refused during each(), or
    // if every one of 2^32 - 1 slots is in use or retired.
    [[nodiscard]] Entity create();
    // Destroys an entity and all its components. False if the handle is not valid or during
    // each().
    bool destroy(Entity entity);
    [[nodiscard]] bool alive(Entity entity) const;
    // Number of live entities.
    [[nodiscard]] std::size_t size() const { return live_; }

    // Adds a T built from args. Nullptr if the entity is not alive, already has a T, or
    // during each().
    template <class T, class... Args>
    T* add(Entity entity, Args&&... args)
    {
        static_assert(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_assignable_v<T>
                          && std::is_nothrow_destructible_v<T>,
                      "ECS components require nonthrowing move and destruction");
        if (iterating_ != 0 || !alive(entity)) {
            return nullptr;
        }
        detail::Pool<T>& pool = pool_for<T>();
        if (pool.find(entity.index) != nullptr) {
            return nullptr;
        }
        return &pool.emplace(entity.index, std::forward<Args>(args)...);
    }

    // The entity's T, or nullptr if it has none or the handle is not valid.
    template <class T>
    [[nodiscard]] T* get(Entity entity)
    {
        detail::Pool<T>* pool = existing_pool<T>();
        return pool != nullptr && alive(entity) ? pool->find(entity.index) : nullptr;
    }
    template <class T>
    [[nodiscard]] const T* get(Entity entity) const
    {
        const detail::Pool<T>* pool = existing_pool<T>();
        return pool != nullptr && alive(entity) ? pool->find(entity.index) : nullptr;
    }
    template <class T>
    [[nodiscard]] bool has(Entity entity) const
    {
        return get<T>(entity) != nullptr;
    }

    // Removes the entity's T. False if it has none, the handle is not valid, or during each().
    template <class T>
    bool remove(Entity entity)
    {
        detail::Pool<T>* pool = existing_pool<T>();
        if (iterating_ != 0 || pool == nullptr || !alive(entity)) {
            return false;
        }
        const IterationGuard guard(iterating_);
        return pool->remove(entity);
    }

    // Sets the hook called before each removal of a T (see the class comment); an empty
    // function clears it. False during each(), a hook, or for a subsystem-owned hook.
    template <class T>
    bool on_remove(std::function<void(Entity, T&)> hook)
    {
        if (iterating_ != 0) {
            return false;
        }
        detail::Pool<T>& pool = pool_for<T>();
        if (pool.owned_hook()) {
            return false;
        }
        pool.set_on_remove(std::move(hook));
        return true;
    }

    // Exclusively claim two empty component pools for a subsystem. The shared owner pins
    // cleanup context after the helper dies. Refused without changing either hook.
    template <class A, class B>
    bool claim_removal_hooks(std::shared_ptr<void> owner,
                             std::function<void(Entity, A&)> first,
                             std::function<void(Entity, B&)> second)
    {
        static_assert(!std::is_same_v<A, B>);
        if (locked() || !owner || !first || !second) {
            return false;
        }
        auto& a = pool_for<A>();
        auto& b = pool_for<B>();
        if (a.owned_hook() || b.owned_hook() || a.has_hook() || b.has_hook() || a.size() != 0 || b.size() != 0) {
            return false;
        }
        a.claim_hook(owner, std::move(first));
        b.claim_hook(owner, std::move(second));
        return true;
    }

    // True while each() or a removal hook runs: structural changes are refused.
    [[nodiscard]] bool locked() const { return iterating_ != 0; }

    // Number of entities with a T.
    template <class T>
    [[nodiscard]] std::size_t count() const
    {
        const detail::Pool<T>* pool = existing_pool<T>();
        return pool != nullptr ? pool->size() : 0;
    }

    // Calls fn(Entity, First&, Rest&...) for every entity that has all the components, in the
    // order the First components sit in storage. See the class comment for what fn may do.
    template <class First, class... Rest, class Fn>
    void each(Fn&& fn)
    {
        detail::Pool<First>* first = existing_pool<First>();
        if (first == nullptr || ((existing_pool<Rest>() == nullptr) || ...)) {
            return;
        }
        const IterationGuard guard(iterating_);
        for (std::size_t position = 0; position < first->size(); ++position) {
            const std::uint32_t index = first->slot_at(position);
            visit<Rest...>(fn, Entity{index, generations_[index], owner_}, first->value_at(position));
        }
    }

    // Read-only iteration for render snapshots/inspection; structural changes remain locked.
    template <class First, class... Rest, class Fn>
    void each(Fn&& fn) const
    {
        const detail::Pool<First>* first = existing_pool<First>();
        if (first == nullptr || ((existing_pool<Rest>() == nullptr) || ...)) {
            return;
        }
        const IterationGuard guard(iterating_);
        for (std::size_t position = 0; position < first->size(); ++position) {
            const std::uint32_t index = first->slot_at(position);
            const std::tuple<const Rest*...> others{existing_pool<Rest>()->find(index)...};
            std::apply([&](const Rest*... other) {
                if (((other != nullptr) && ...)) {
                    fn(Entity{index, generations_[index], owner_}, first->value_at(position), *other...);
                }
            }, others);
        }
    }

private:
    struct IterationGuard {
        explicit IterationGuard(std::uint32_t& counter) : counter_(counter) { ++counter_; }
        IterationGuard(const IterationGuard&) = delete;
        IterationGuard& operator=(const IterationGuard&) = delete;
        IterationGuard(IterationGuard&&) = delete;
        IterationGuard& operator=(IterationGuard&&) = delete;
        ~IterationGuard() { --counter_; }

    private:
        std::uint32_t& counter_;
    };

    template <class... Rest, class Fn, class First>
    void visit(Fn& fn, Entity entity, First& value)
    {
        if constexpr (sizeof...(Rest) == 0) {
            fn(entity, value);
        } else {
            const std::tuple<Rest*...> others{existing_pool<Rest>()->find(entity.index)...};
            std::apply(
                [&](Rest*... other) {
                    if (((other != nullptr) && ...)) {
                        fn(entity, value, *other...);
                    }
                },
                others);
        }
    }

    template <class T>
    detail::Pool<T>& pool_for()
    {
        const detail::TypeId id = detail::type_id<T>();
        if (id >= pools_.size()) {
            pools_.resize(static_cast<std::size_t>(id) + 1);
        }
        if (!pools_[id]) {
            auto pool = std::make_unique<detail::Pool<T>>();
            removal_order_.push_back(pool.get());
            pools_[id] = std::move(pool);
        }
        return static_cast<detail::Pool<T>&>(*pools_[id]);
    }

    template <class T>
    [[nodiscard]] detail::Pool<T>* existing_pool() const
    {
        const detail::TypeId id = detail::type_id<T>();
        return id < pools_.size() && pools_[id] ? static_cast<detail::Pool<T>*>(pools_[id].get()) : nullptr;
    }

    // Destroys every live entity in slot order, running removal hooks.
    void destroy_all();
    [[nodiscard]] bool has_owned_hooks() const;
    std::uint64_t owner_ = 0;

    // generations_[slot] is the slot's current generation; a slot is alive when alive_[slot].
    std::vector<std::uint32_t> generations_;
    std::vector<std::uint8_t> alive_;
    std::vector<std::uint32_t> free_;
    std::vector<std::unique_ptr<detail::PoolBase>> pools_;
    std::vector<detail::PoolBase*> removal_order_;
    std::size_t live_ = 0;
    mutable std::uint32_t iterating_ = 0;
};


// Changes recorded now and applied later, typically while iterating: each() refuses structural
// changes, so a system records them here and the caller applies the buffer after each()
// returns. Changes apply in the order recorded, so the result is deterministic.
class ChangeBuffer {
public:
    // Creates an entity, then calls setup(world, entity) to give it components.
    template <class Setup>
    void create(Setup setup)
    {
        push([setup = std::move(setup)](World& world) mutable {
            const Entity entity = world.create();
            if (!world.alive(entity)) {
                return false;
            }
            try {
                setup(world, entity);
            } catch (...) {
                world.destroy(entity);
                throw;
            }
            return true;
        });
    }
    void destroy(Entity entity)
    {
        push([entity](World& world) { return world.destroy(entity); });
    }
    template <class T>
    void add(Entity entity, T value)
    {
        push([entity, value = std::move(value)](World& world) mutable {
            return world.add<T>(entity, std::move(value)) != nullptr;
        });
    }
    template <class T>
    void remove(Entity entity)
    {
        push([entity](World& world) { return world.remove<T>(entity); });
    }

    // Applies every change in order and empties the buffer. Returns how many took effect: a
    // change does nothing if, when its turn comes, the world refuses it (for example the
    // entity was destroyed by an earlier change). If the world is locked (inside each() or a
    // hook), nothing is applied, the buffer is kept and the result is 0. On exception the
    // failing command is consumed and later commands remain queued; prior changes stay applied.
    std::size_t apply(World& world);

    [[nodiscard]] std::size_t size() const { return changes_.size(); }
    [[nodiscard]] bool empty() const { return changes_.empty(); }
    void clear() { changes_.clear(); }

private:
    class Change {
    public:
        Change() = default;
        Change(const Change&) = delete;
        Change& operator=(const Change&) = delete;
        Change(Change&&) = delete;
        Change& operator=(Change&&) = delete;
        virtual ~Change() = default;
        virtual bool apply(World& world) = 0;
    };

    template <class Fn>
    class Recorded final : public Change {
    public:
        explicit Recorded(Fn fn) : fn_(std::move(fn)) {}
        bool apply(World& world) override { return fn_(world); }

    private:
        Fn fn_;
    };

    template <class Fn>
    void push(Fn fn)
    {
        changes_.push_back(std::make_unique<Recorded<Fn>>(std::move(fn)));
    }

    // Move-only callables (components may be move-only, so std::function cannot hold them).
    std::list<std::unique_ptr<Change>> changes_;
};

}
