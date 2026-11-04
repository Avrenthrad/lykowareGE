#include <lykoware/physics.hpp>

#include <lykoware/log.hpp>

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace lykoware::physics {
namespace {

// --- Jolt's process-wide state, shared by every World ------------------------------------

void trace(const char* format, ...)
{
    char message[512];
    std::va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    log(LogLevel::warning, std::string{"Jolt: "} + message);
}

#ifdef JPH_ENABLE_ASSERTS
bool assert_failed(const char* expression, const char* message, const char* file, JPH::uint line)
{
    log(LogLevel::error, std::string{"Jolt assert: "} + expression + " " + (message != nullptr ? message : "") + " ("
                             + file + ":" + std::to_string(line) + ")");
    return false; // report and continue rather than break into a debugger
}
#endif

std::mutex g_runtime_mutex;
int g_runtime_users = 0;

// Held by every World: the first one registers Jolt's allocator, factory and types; the last
// one unregisters them.
class Runtime {
public:
    Runtime()
    {
        const std::lock_guard lock{g_runtime_mutex};
        if (g_runtime_users++ == 0) {
            JPH::RegisterDefaultAllocator();
            JPH::Trace = trace;
            JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = assert_failed;)
            JPH::Factory::sInstance = new JPH::Factory();
            JPH::RegisterTypes();
        }
    }
    ~Runtime()
    {
        const std::lock_guard lock{g_runtime_mutex};
        if (--g_runtime_users == 0) {
            JPH::UnregisterTypes();
            delete JPH::Factory::sInstance;
            JPH::Factory::sInstance = nullptr;
        }
    }
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
};

// --- Collision layers: static bodies never collide with each other ------------------------

namespace layer {
constexpr JPH::ObjectLayer static_bodies = 0;
constexpr JPH::ObjectLayer moving_bodies = 1;
constexpr JPH::uint count = 2;
}

JPH::BroadPhaseLayer broad_phase_of(JPH::ObjectLayer object_layer)
{
    return JPH::BroadPhaseLayer(static_cast<JPH::BroadPhaseLayer::Type>(object_layer));
}

class ObjectPairs final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
    {
        return a == layer::moving_bodies || b == layer::moving_bodies;
    }
};

class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return layer::count; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer object_layer) const override
    {
        return broad_phase_of(object_layer);
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer) const override { return "layer"; }
#endif
};

class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer object_layer, JPH::BroadPhaseLayer broad_phase) const override
    {
        return object_layer == layer::moving_bodies || broad_phase == broad_phase_of(layer::moving_bodies);
    }
};

// --- Conversions -------------------------------------------------------------------------

JPH::Vec3 to_jolt(Vec3 v)
{
    return {v.x, v.y, v.z};
}

JPH::Quat to_jolt(Quat q)
{
    return {q.x, q.y, q.z, q.w};
}

Vec3 from_jolt(JPH::Vec3Arg v)
{
    return {v.GetX(), v.GetY(), v.GetZ()};
}

Quat from_jolt(JPH::QuatArg q)
{
    return {q.GetX(), q.GetY(), q.GetZ(), q.GetW()};
}

bool positive(float value)
{
    return std::isfinite(value) && value > 0.F;
}

// Builds the Jolt shape for a description, or returns null and fills error. The caller takes
// the only reference.
const JPH::Shape* make_shape(const Shape& shape, std::string& error)
{
    if (const auto* box = std::get_if<BoxShape>(&shape)) {
        const Vec3 half = box->half_extents;
        if (!positive(half.x) || !positive(half.y) || !positive(half.z)) {
            error = "box half extents must be positive";
            return nullptr;
        }
        // Jolt rounds box corners by a convex radius that may not exceed the smallest half extent.
        const float convex_radius = std::min({JPH::cDefaultConvexRadius, half.x, half.y, half.z});
        return new JPH::BoxShape(to_jolt(half), convex_radius);
    }
    if (const auto* sphere = std::get_if<SphereShape>(&shape)) {
        if (!positive(sphere->radius)) {
            error = "sphere radius must be positive";
            return nullptr;
        }
        return new JPH::SphereShape(sphere->radius);
    }
    const auto& capsule = std::get<CapsuleShape>(shape);
    if (!positive(capsule.half_height) || !positive(capsule.radius)) {
        error = "capsule half height and radius must be positive";
        return nullptr;
    }
    return new JPH::CapsuleShape(capsule.half_height, capsule.radius);
}

JPH::EMotionType to_jolt(MotionType motion)
{
    switch (motion) {
    case MotionType::Static:
        return JPH::EMotionType::Static;
    case MotionType::Kinematic:
        return JPH::EMotionType::Kinematic;
    case MotionType::Dynamic:
        break;
    }
    return JPH::EMotionType::Dynamic;
}

class StateHash {
public:
    void add(float value)
    {
        const auto bits = std::bit_cast<std::uint32_t>(value);
        for (int shift = 0; shift < 32; shift += 8) {
            hash_ = (hash_ ^ ((bits >> shift) & 0xFFU)) * 0x100000001b3ULL;
        }
    }
    void add(Vec3 v)
    {
        add(v.x);
        add(v.y);
        add(v.z);
    }
    [[nodiscard]] std::uint64_t value() const { return hash_; }

private:
    std::uint64_t hash_ = 0xcbf29ce484222325ULL;
};

JPH::uint worker_count(std::uint32_t requested)
{
    if (requested != 0) {
        return requested;
    }
    const unsigned hardware = std::thread::hardware_concurrency();
    return std::clamp<JPH::uint>(hardware > 1 ? hardware - 1 : 1, 1, 8);
}

}

// --- World -------------------------------------------------------------------------------

struct World::Impl {
    explicit Impl(const WorldConfig& config)
        : temp_allocator(config.step_memory_bytes)
        , jobs(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, static_cast<int>(worker_count(config.worker_threads)))
        , max_bodies(config.max_bodies)
    {
        system.Init(config.max_bodies, 0, config.max_body_pairs, config.max_contact_constraints, broad_phase_layers,
                    object_vs_broad_phase, object_pairs);
        system.SetGravity(to_jolt(config.gravity));
    }

    ~Impl()
    {
        JPH::BodyInterface& bodies = system.GetBodyInterface();
        for (const Slot& slot : slots) {
            if (slot.alive) {
                bodies.RemoveBody(slot.jolt);
                bodies.DestroyBody(slot.jolt);
            }
        }
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    struct Slot {
        JPH::BodyID jolt;
        std::uint32_t generation = 0;
        bool alive = false;
    };

    [[nodiscard]] const Slot* find(BodyId id) const
    {
        if (id.generation == 0 || id.index >= slots.size()) {
            return nullptr;
        }
        const Slot& slot = slots[id.index];
        return slot.alive && slot.generation == id.generation ? &slot : nullptr;
    }

    // Declared first so it is destroyed last: everything below uses Jolt's allocator.
    Runtime runtime;
    JPH::TempAllocatorImpl temp_allocator;
    JPH::JobSystemThreadPool jobs;
    BroadPhaseLayers broad_phase_layers;
    ObjectVsBroadPhase object_vs_broad_phase;
    ObjectPairs object_pairs;
    JPH::PhysicsSystem system;

    std::uint32_t max_bodies;
    std::vector<Slot> slots;
    // Freed slot indices, reused last-in first-out so the order is deterministic.
    std::vector<std::uint32_t> free_slots;
    std::size_t live = 0;
};

World::World(std::unique_ptr<Impl> impl)
    : impl_{std::move(impl)}
{
}

World::~World() = default;

std::unique_ptr<World> World::create(const WorldConfig& config, std::string& error)
{
    error.clear();
    if (config.max_bodies < 1 || config.max_bodies > JPH::BodyID::cMaxBodyIndex) {
        error = "max_bodies must be from 1 to " + std::to_string(JPH::BodyID::cMaxBodyIndex);
        return nullptr;
    }
    if (config.max_body_pairs < 1 || config.max_contact_constraints < 1) {
        error = "max_body_pairs and max_contact_constraints must be at least 1";
        return nullptr;
    }
    if (config.step_memory_bytes < 64U * 1024U) {
        error = "step_memory_bytes must be at least 64 KiB";
        return nullptr;
    }
    if (!std::isfinite(config.gravity.x) || !std::isfinite(config.gravity.y) || !std::isfinite(config.gravity.z)) {
        error = "gravity must be finite";
        return nullptr;
    }
    return std::unique_ptr<World>(new World(std::make_unique<Impl>(config)));
}

std::optional<BodyId> World::create_body(const BodyDesc& desc, std::string& error)
{
    error.clear();
    Impl& impl = *impl_;
    if (impl.live >= impl.max_bodies) {
        error = "the world already has max_bodies (" + std::to_string(impl.max_bodies) + ") bodies";
        return std::nullopt;
    }
    const JPH::Quat rotation = to_jolt(desc.rotation);
    if (rotation.IsNaN() || rotation.LengthSq() < 1e-12F) {
        error = "rotation must be a non-zero quaternion";
        return std::nullopt;
    }
    const JPH::Shape* shape = make_shape(desc.shape, error);
    if (shape == nullptr) {
        return std::nullopt;
    }

    // Reserve the slot first; its index is stored in the Jolt body so ray hits map back to it.
    std::uint32_t index = 0;
    if (!impl.free_slots.empty()) {
        index = impl.free_slots.back();
        impl.free_slots.pop_back();
    } else {
        index = static_cast<std::uint32_t>(impl.slots.size());
        impl.slots.emplace_back();
    }

    const bool is_static = desc.motion == MotionType::Static;
    // The settings take the only reference to the new shape; the body adds its own.
    JPH::BodyCreationSettings settings{shape, JPH::RVec3(to_jolt(desc.position)), rotation.Normalized(),
                                       to_jolt(desc.motion), is_static ? layer::static_bodies : layer::moving_bodies};
    settings.mFriction = desc.friction;
    settings.mRestitution = desc.restitution;
    settings.mUserData = index;
    if (!is_static) {
        settings.mLinearVelocity = to_jolt(desc.linear_velocity);
        settings.mAngularVelocity = to_jolt(desc.angular_velocity);
    }
    const JPH::BodyID jolt = impl.system.GetBodyInterface().CreateAndAddBody(
        settings, is_static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
    if (jolt.IsInvalid()) {
        impl.free_slots.push_back(index);
        error = "Jolt could not create the body";
        return std::nullopt;
    }

    Impl::Slot& slot = impl.slots[index];
    slot.jolt = jolt;
    slot.alive = true;
    // Never generation 0, which marks the default (invalid) handle.
    slot.generation = slot.generation == UINT32_MAX ? 1 : slot.generation + 1;
    ++impl.live;
    return BodyId{index, slot.generation};
}

bool World::destroy_body(BodyId body)
{
    Impl& impl = *impl_;
    if (impl.find(body) == nullptr) {
        return false;
    }
    Impl::Slot& slot = impl.slots[body.index];
    JPH::BodyInterface& bodies = impl.system.GetBodyInterface();
    bodies.RemoveBody(slot.jolt);
    bodies.DestroyBody(slot.jolt);
    slot.alive = false;
    slot.jolt = JPH::BodyID{};
    impl.free_slots.push_back(body.index);
    --impl.live;
    return true;
}

bool World::is_valid(BodyId body) const
{
    return impl_->find(body) != nullptr;
}

std::size_t World::body_count() const
{
    return impl_->live;
}

std::optional<BodyState> World::state(BodyId body) const
{
    const Impl::Slot* slot = impl_->find(body);
    if (slot == nullptr) {
        return std::nullopt;
    }
    const JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
    JPH::RVec3 position;
    JPH::Quat rotation;
    bodies.GetPositionAndRotation(slot->jolt, position, rotation);
    return BodyState{from_jolt(JPH::Vec3(position)), from_jolt(rotation), from_jolt(bodies.GetLinearVelocity(slot->jolt)),
                     from_jolt(bodies.GetAngularVelocity(slot->jolt))};
}

bool World::set_velocity(BodyId body, Vec3 linear, Vec3 angular)
{
    const Impl::Slot* slot = impl_->find(body);
    if (slot == nullptr) {
        return false;
    }
    JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
    if (bodies.GetMotionType(slot->jolt) == JPH::EMotionType::Static) {
        return false;
    }
    bodies.SetLinearAndAngularVelocity(slot->jolt, to_jolt(linear), to_jolt(angular));
    bodies.ActivateBody(slot->jolt);
    return true;
}

bool World::step(float seconds)
{
    if (!positive(seconds)) {
        return false;
    }
    Impl& impl = *impl_;
    return impl.system.Update(seconds, 1, &impl.temp_allocator, &impl.jobs) == JPH::EPhysicsUpdateError::None;
}

std::optional<RayHit> World::cast_ray(Vec3 origin, Vec3 direction) const
{
    const JPH::Vec3 jolt_direction = to_jolt(direction);
    if (!jolt_direction.IsNaN() && jolt_direction.LengthSq() > 0.F) {
        const JPH::RRayCast ray{JPH::RVec3(to_jolt(origin)), jolt_direction};
        JPH::RayCastResult hit;
        if (impl_->system.GetNarrowPhaseQuery().CastRay(ray, hit)) {
            const auto index = static_cast<std::uint32_t>(impl_->system.GetBodyInterface().GetUserData(hit.mBodyID));
            if (index < impl_->slots.size() && impl_->slots[index].alive) {
                return RayHit{BodyId{index, impl_->slots[index].generation}, hit.mFraction,
                              from_jolt(JPH::Vec3(ray.GetPointOnRay(hit.mFraction)))};
            }
        }
    }
    return std::nullopt;
}

std::uint64_t World::state_hash() const
{
    StateHash hash;
    for (std::uint32_t index = 0; index < impl_->slots.size(); ++index) {
        if (!impl_->slots[index].alive) {
            continue;
        }
        const std::optional<BodyState> body = state(BodyId{index, impl_->slots[index].generation});
        hash.add(body->position);
        hash.add(body->rotation.x);
        hash.add(body->rotation.y);
        hash.add(body->rotation.z);
        hash.add(body->rotation.w);
        hash.add(body->linear_velocity);
        hash.add(body->angular_velocity);
    }
    return hash.value();
}

}
