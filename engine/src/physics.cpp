#include <lykoware/physics.hpp>

#include <lykoware/log.hpp>

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Geometry/Plane.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
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

bool finite(Vec3 v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

constexpr std::uint32_t max_height_field_samples = 4096;

// Validates a shape description and builds Jolt's settings for it, or returns null and fills
// error. The caller takes the only reference; Jolt builds the shape when the body is created.
JPH::ShapeSettings* make_shape_settings(const Shape& shape, std::string& error)
{
    if (const auto* box = std::get_if<BoxShape>(&shape)) {
        const Vec3 half = box->half_extents;
        if (!positive(half.x) || !positive(half.y) || !positive(half.z)) {
            error = "box half extents must be positive";
            return nullptr;
        }
        // Jolt rounds box corners by a convex radius that may not exceed the smallest half extent.
        const float convex_radius = std::min({JPH::cDefaultConvexRadius, half.x, half.y, half.z});
        return new JPH::BoxShapeSettings(to_jolt(half), convex_radius);
    }
    if (const auto* sphere = std::get_if<SphereShape>(&shape)) {
        if (!positive(sphere->radius)) {
            error = "sphere radius must be positive";
            return nullptr;
        }
        return new JPH::SphereShapeSettings(sphere->radius);
    }
    if (const auto* capsule = std::get_if<CapsuleShape>(&shape)) {
        if (!positive(capsule->half_height) || !positive(capsule->radius)) {
            error = "capsule half height and radius must be positive";
            return nullptr;
        }
        return new JPH::CapsuleShapeSettings(capsule->half_height, capsule->radius);
    }
    const auto& field = std::get<HeightFieldShape>(shape);
    const std::uint32_t n = field.samples_per_side;
    if (n < 4 || n > max_height_field_samples || n % 2 != 0) {
        error = "height field samples_per_side must be even, from 4 to " + std::to_string(max_height_field_samples);
        return nullptr;
    }
    if (field.heights.size() != static_cast<std::size_t>(n) * n) {
        error = "height field needs samples_per_side squared heights";
        return nullptr;
    }
    if (!positive(field.spacing)) {
        error = "height field spacing must be positive";
        return nullptr;
    }
    if (!std::all_of(field.heights.begin(), field.heights.end(), [](float h) { return std::isfinite(h); })) {
        error = "height field heights must be finite";
        return nullptr;
    }
    auto* settings = new JPH::HeightFieldShapeSettings(field.heights.data(), JPH::Vec3::sZero(),
                                                       JPH::Vec3(field.spacing, 1.F, field.spacing), n);
    settings->mBitsPerSample = settings->CalculateBitsPerSampleForError(0.01F);
    return settings;
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
    void add(std::uint32_t bits)
    {
        for (int shift = 0; shift < 32; shift += 8) {
            hash_ = (hash_ ^ ((bits >> shift) & 0xFFU)) * 0x100000001b3ULL;
        }
    }
    void add(float value) { add(std::bit_cast<std::uint32_t>(value)); }
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

// Handles for one kind of object: {index, generation} into a table of slots. A freed slot is
// reused last-in first-out (a deterministic order) with a new generation, so a handle to a
// destroyed object never reaches a later one.
template <class Payload>
class Slots {
public:
    struct Slot {
        Payload payload{};
        std::uint32_t generation = 0;
        bool alive = false;
    };

    // Index for a new object; not alive until activate().
    std::uint32_t reserve()
    {
        if (!free_.empty()) {
            const std::uint32_t index = free_.back();
            free_.pop_back();
            return index;
        }
        slots_.emplace_back();
        return static_cast<std::uint32_t>(slots_.size() - 1);
    }
    // Gives a reserved index back after a failed creation.
    void unreserve(std::uint32_t index) { free_.push_back(index); }
    // Makes a reserved slot live; returns its new generation (never 0, which marks no object).
    std::uint32_t activate(std::uint32_t index, Payload payload)
    {
        Slot& slot = slots_[index];
        slot.payload = std::move(payload);
        slot.alive = true;
        slot.generation = slot.generation == UINT32_MAX ? 1 : slot.generation + 1;
        ++live_;
        return slot.generation;
    }
    [[nodiscard]] Payload* find(std::uint32_t index, std::uint32_t generation)
    {
        return const_cast<Payload*>(std::as_const(*this).find(index, generation));
    }
    [[nodiscard]] const Payload* find(std::uint32_t index, std::uint32_t generation) const
    {
        if (generation == 0 || index >= slots_.size()) {
            return nullptr;
        }
        const Slot& slot = slots_[index];
        return slot.alive && slot.generation == generation ? &slot.payload : nullptr;
    }
    void release(std::uint32_t index)
    {
        Slot& slot = slots_[index];
        slot.alive = false;
        slot.payload = Payload{};
        free_.push_back(index);
        --live_;
    }
    [[nodiscard]] std::size_t live() const { return live_; }
    [[nodiscard]] std::uint32_t generation(std::uint32_t index) const { return slots_[index].generation; }
    [[nodiscard]] bool alive(std::uint32_t index) const { return index < slots_.size() && slots_[index].alive; }
    [[nodiscard]] std::uint32_t size() const { return static_cast<std::uint32_t>(slots_.size()); }
    [[nodiscard]] Payload& payload(std::uint32_t index) { return slots_[index].payload; }
    [[nodiscard]] const Payload& payload(std::uint32_t index) const { return slots_[index].payload; }

private:
    std::vector<Slot> slots_;
    std::vector<std::uint32_t> free_;
    std::size_t live_ = 0;
};

struct Character {
    JPH::Ref<JPH::CharacterVirtual> jolt;
    JPH::Vec3 input = JPH::Vec3::sZero();
    float jump = 0.F;
    float step_height = 0.F;
};

GroundState from_jolt(JPH::CharacterBase::EGroundState state)
{
    switch (state) {
    case JPH::CharacterBase::EGroundState::OnGround:
        return GroundState::OnGround;
    case JPH::CharacterBase::EGroundState::OnSteepGround:
        return GroundState::OnSteepGround;
    case JPH::CharacterBase::EGroundState::NotSupported:
        return GroundState::NotSupported;
    case JPH::CharacterBase::EGroundState::InAir:
        break;
    }
    return GroundState::InAir;
}

// Stateless and shared by every world; they outlive all of them.
const BroadPhaseLayers g_broad_phase_layers;
const ObjectVsBroadPhase g_object_vs_broad_phase;
const ObjectPairs g_object_pairs;

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

// Runtime is a base class so it is constructed before, and destroyed after, every member:
// they all use Jolt's allocator.
struct World::Impl : private Runtime {
    explicit Impl(const WorldConfig& config)
        : jobs(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, static_cast<int>(worker_count(config.worker_threads)))
        , temp_allocator(config.step_memory_bytes)
        , max_bodies(config.max_bodies)
    {
        system.Init(config.max_bodies, 0, config.max_body_pairs, config.max_contact_constraints, g_broad_phase_layers,
                    g_object_vs_broad_phase, g_object_pairs);
        system.SetGravity(to_jolt(config.gravity));
    }

    ~Impl()
    {
        // Characters refer to the physics system; release them first.
        for (std::uint32_t index = 0; index < characters.size(); ++index) {
            characters.payload(index).jolt = nullptr;
        }
        JPH::BodyInterface& interface = system.GetBodyInterface();
        for (std::uint32_t index = 0; index < bodies.size(); ++index) {
            if (bodies.alive(index)) {
                interface.RemoveBody(bodies.payload(index));
                interface.DestroyBody(bodies.payload(index));
            }
        }
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    // The Lykoware handle of a Jolt body, from the slot index stored in its user data.
    [[nodiscard]] std::optional<BodyId> body_handle(const JPH::BodyID& jolt) const
    {
        if (jolt.IsInvalid()) {
            return std::nullopt;
        }
        const auto index = static_cast<std::uint32_t>(system.GetBodyInterface().GetUserData(jolt));
        if (!bodies.alive(index)) {
            return std::nullopt;
        }
        return BodyId{index, bodies.generation(index)};
    }

    void move_character(Character& character, float seconds)
    {
        JPH::CharacterVirtual& jolt = *character.jolt;
        const JPH::Vec3 up = jolt.GetUp();
        const JPH::Vec3 gravity = system.GetGravity();
        const JPH::Vec3 vertical = up * jolt.GetLinearVelocity().Dot(up);
        const JPH::Vec3 ground_velocity = jolt.GetGroundVelocity();
        const bool moving_towards_ground = (vertical - ground_velocity).Dot(up) < 0.1F;

        // On walkable ground the character moves with the ground and may jump; otherwise it
        // keeps its vertical speed. Gravity applies either way.
        JPH::Vec3 velocity = vertical;
        if (jolt.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround && moving_towards_ground) {
            velocity = ground_velocity;
            if (character.jump > 0.F) {
                velocity += up * character.jump;
                character.jump = 0.F;
            }
        }
        velocity += gravity * seconds;
        velocity += character.input - up * character.input.Dot(up);
        jolt.SetLinearVelocity(velocity);

        JPH::CharacterVirtual::ExtendedUpdateSettings settings;
        settings.mWalkStairsStepUp = up * character.step_height;
        jolt.ExtendedUpdate(seconds, gravity, settings, system.GetDefaultBroadPhaseLayerFilter(layer::moving_bodies),
                            system.GetDefaultLayerFilter(layer::moving_bodies), {}, {}, temp_allocator);
    }

    JPH::JobSystemThreadPool jobs;
    JPH::PhysicsSystem system;
    Slots<JPH::BodyID> bodies;
    Slots<Character> characters;
    JPH::TempAllocatorImpl temp_allocator;
    std::uint32_t max_bodies;
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
    if (!finite(config.gravity)) {
        error = "gravity must be finite";
        return nullptr;
    }
    return std::unique_ptr<World>(new World(std::make_unique<Impl>(config)));
}

std::optional<BodyId> World::create_body(const BodyDesc& desc, std::string& error)
{
    error.clear();
    Impl& impl = *impl_;
    if (impl.bodies.live() >= impl.max_bodies) {
        error = "the world already has max_bodies (" + std::to_string(impl.max_bodies) + ") bodies";
        return std::nullopt;
    }
    const JPH::Quat rotation = to_jolt(desc.rotation);
    if (rotation.IsNaN() || rotation.LengthSq() < 1e-12F) {
        error = "rotation must be a non-zero quaternion";
        return std::nullopt;
    }
    if (std::holds_alternative<HeightFieldShape>(desc.shape) && desc.motion != MotionType::Static) {
        error = "a height field must be static";
        return std::nullopt;
    }
    JPH::ShapeSettings* shape = make_shape_settings(desc.shape, error);
    if (shape == nullptr) {
        return std::nullopt;
    }

    // Reserve the slot first; its index is stored in the Jolt body so queries map back to it.
    const std::uint32_t index = impl.bodies.reserve();
    const bool is_static = desc.motion == MotionType::Static;
    // The creation settings take the only reference to the shape settings.
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
        impl.bodies.unreserve(index);
        error = "Jolt could not create the body";
        return std::nullopt;
    }
    return BodyId{index, impl.bodies.activate(index, jolt)};
}

bool World::destroy_body(BodyId body)
{
    Impl& impl = *impl_;
    const JPH::BodyID* jolt = impl.bodies.find(body.index, body.generation);
    if (jolt == nullptr) {
        return false;
    }
    JPH::BodyInterface& interface = impl.system.GetBodyInterface();
    interface.RemoveBody(*jolt);
    interface.DestroyBody(*jolt);
    impl.bodies.release(body.index);
    return true;
}

bool World::is_valid(BodyId body) const
{
    return impl_->bodies.find(body.index, body.generation) != nullptr;
}

std::size_t World::body_count() const
{
    return impl_->bodies.live();
}

std::optional<BodyState> World::state(BodyId body) const
{
    const JPH::BodyID* jolt = impl_->bodies.find(body.index, body.generation);
    if (jolt == nullptr) {
        return std::nullopt;
    }
    const JPH::BodyInterface& interface = impl_->system.GetBodyInterface();
    JPH::RVec3 position;
    JPH::Quat rotation;
    interface.GetPositionAndRotation(*jolt, position, rotation);
    return BodyState{from_jolt(JPH::Vec3(position)), from_jolt(rotation), from_jolt(interface.GetLinearVelocity(*jolt)),
                     from_jolt(interface.GetAngularVelocity(*jolt))};
}

bool World::set_velocity(BodyId body, Vec3 linear, Vec3 angular)
{
    const JPH::BodyID* jolt = impl_->bodies.find(body.index, body.generation);
    if (jolt == nullptr) {
        return false;
    }
    JPH::BodyInterface& interface = impl_->system.GetBodyInterface();
    if (interface.GetMotionType(*jolt) == JPH::EMotionType::Static) {
        return false;
    }
    interface.SetLinearAndAngularVelocity(*jolt, to_jolt(linear), to_jolt(angular));
    interface.ActivateBody(*jolt);
    return true;
}

std::optional<CharacterId> World::create_character(const CharacterDesc& desc, std::string& error)
{
    error.clear();
    if (!positive(desc.radius) || !positive(desc.cylinder_half_height)) {
        error = "character radius and cylinder_half_height must be positive";
        return std::nullopt;
    }
    if (!positive(desc.max_slope) || desc.max_slope >= 1.5707963F) {
        error = "character max_slope must be between 0 and pi/2 radians";
        return std::nullopt;
    }
    if (!std::isfinite(desc.step_height) || desc.step_height < 0.F) {
        error = "character step_height must be zero or more";
        return std::nullopt;
    }
    if (!positive(desc.mass)) {
        error = "character mass must be positive";
        return std::nullopt;
    }
    if (!finite(desc.position)) {
        error = "character position must be finite";
        return std::nullopt;
    }

    Impl& impl = *impl_;
    JPH::CharacterVirtualSettings settings;
    settings.mShape = new JPH::CapsuleShape(desc.cylinder_half_height, desc.radius);
    // The character's position is its feet: the capsule sits above it.
    settings.mShapeOffset = JPH::Vec3(0.F, desc.cylinder_half_height + desc.radius, 0.F);
    // Only contacts below the centre of the lower cap hold the character up.
    settings.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -desc.radius);
    settings.mMaxSlopeAngle = desc.max_slope;
    settings.mMass = desc.mass;

    const std::uint32_t index = impl.characters.reserve();
    Character character;
    character.jolt = new JPH::CharacterVirtual(&settings, JPH::RVec3(to_jolt(desc.position)), JPH::Quat::sIdentity(),
                                               index, &impl.system);
    character.step_height = desc.step_height;
    return CharacterId{index, impl.characters.activate(index, std::move(character))};
}

bool World::destroy_character(CharacterId character)
{
    if (impl_->characters.find(character.index, character.generation) == nullptr) {
        return false;
    }
    impl_->characters.release(character.index);
    return true;
}

bool World::is_valid(CharacterId character) const
{
    return impl_->characters.find(character.index, character.generation) != nullptr;
}

std::size_t World::character_count() const
{
    return impl_->characters.live();
}

bool World::set_character_input(CharacterId character, Vec3 horizontal_velocity, float jump_speed)
{
    Character* found = impl_->characters.find(character.index, character.generation);
    if (found == nullptr || !finite(horizontal_velocity) || !std::isfinite(jump_speed) || jump_speed < 0.F) {
        return false;
    }
    found->input = to_jolt(horizontal_velocity);
    found->jump = jump_speed;
    return true;
}

std::optional<CharacterState> World::character_state(CharacterId character) const
{
    const Character* found = impl_->characters.find(character.index, character.generation);
    if (found == nullptr) {
        return std::nullopt;
    }
    const JPH::CharacterVirtual& jolt = *found->jolt;
    CharacterState state;
    state.position = from_jolt(JPH::Vec3(jolt.GetPosition()));
    state.velocity = from_jolt(jolt.GetLinearVelocity());
    state.ground = from_jolt(jolt.GetGroundState());
    state.ground_body = impl_->body_handle(jolt.GetGroundBodyID());
    state.ground_normal = from_jolt(jolt.GetGroundNormal());
    return state;
}

bool World::step(float seconds)
{
    if (!positive(seconds)) {
        return false;
    }
    Impl& impl = *impl_;
    for (std::uint32_t index = 0; index < impl.characters.size(); ++index) {
        if (impl.characters.alive(index)) {
            impl.move_character(impl.characters.payload(index), seconds);
        }
    }
    return impl.system.Update(seconds, 1, &impl.temp_allocator, &impl.jobs) == JPH::EPhysicsUpdateError::None;
}

std::optional<RayHit> World::cast_ray(Vec3 origin, Vec3 direction) const
{
    const JPH::Vec3 jolt_direction = to_jolt(direction);
    if (!finite(origin) || jolt_direction.IsNaN() || jolt_direction.LengthSq() <= 0.F) {
        return std::nullopt;
    }
    const JPH::RRayCast ray{JPH::RVec3(to_jolt(origin)), jolt_direction};
    JPH::RayCastResult hit;
    if (!impl_->system.GetNarrowPhaseQuery().CastRay(ray, hit)) {
        return std::nullopt;
    }
    const std::optional<BodyId> body = impl_->body_handle(hit.mBodyID);
    if (!body) {
        return std::nullopt;
    }
    return RayHit{*body, hit.mFraction, from_jolt(JPH::Vec3(ray.GetPointOnRay(hit.mFraction)))};
}

std::uint64_t World::state_hash() const
{
    StateHash hash;
    for (std::uint32_t index = 0; index < impl_->bodies.size(); ++index) {
        if (!impl_->bodies.alive(index)) {
            continue;
        }
        const std::optional<BodyState> body = state(BodyId{index, impl_->bodies.generation(index)});
        hash.add(body->position);
        hash.add(body->rotation.x);
        hash.add(body->rotation.y);
        hash.add(body->rotation.z);
        hash.add(body->rotation.w);
        hash.add(body->linear_velocity);
        hash.add(body->angular_velocity);
    }
    for (std::uint32_t index = 0; index < impl_->characters.size(); ++index) {
        if (!impl_->characters.alive(index)) {
            continue;
        }
        const std::optional<CharacterState> character = character_state(CharacterId{index, impl_->characters.generation(index)});
        hash.add(character->position);
        hash.add(character->velocity);
        hash.add(static_cast<std::uint32_t>(character->ground));
    }
    return hash.value();
}

}
