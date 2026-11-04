// Physics spike (ADR 0003, ADR 0018): vendored Jolt Physics builds, the two Lykoware
// replacements of its Numerical Recipes-derived functions are correct, and the simulation
// is deterministic. Tests call Jolt directly; game code will use a Lykoware wrapper.
//
// Usage: lykoware_physics_jolt_test math | determinism
//   math         EigenValueSymmetric and GaussianElimination against known answers, and the
//                inertia decomposition Jolt runs for every dynamic body.
//   determinism  A 64-body scene stepped 300 fixed 60 Hz ticks: two runs give the same state,
//                save/restore mid-run reproduces it, nothing falls through the ground, and the
//                final state hash equals expected_hash. Jolt is built with
//                CROSS_PLATFORM_DETERMINISTIC, so every CI job (MSVC and GCC, Debug and
//                Release, sanitizers) must produce that same hash.

#include <lykoware/clock.hpp>

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Math/EigenValueSymmetric.h>
#include <Jolt/Math/GaussianElimination.h>
#include <Jolt/Math/Matrix.h>
#include <Jolt/Math/Vector.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/MassProperties.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/StateRecorderImpl.h>
#include <Jolt/RegisterTypes.h>

#include <bit>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

int g_failures = 0;
int g_jolt_asserts = 0;

void expect(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

bool close_to(float a, float b, float tolerance)
{
    return std::fabs(a - b) <= tolerance;
}

void trace(const char* format, ...)
{
    std::va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
    std::fputc('\n', stderr);
}

#ifdef JPH_ENABLE_ASSERTS
bool assert_failed(const char* expression, const char* message, const char* file, JPH::uint line)
{
    std::cerr << "Jolt assert: " << expression << ' ' << (message != nullptr ? message : "") << " (" << file << ':'
              << line << ")\n";
    ++g_jolt_asserts;
    return false; // continue; the test reports the failure
}
#endif

// Jolt's process-wide registration: allocator, factory and types.
class JoltRuntime {
public:
    JoltRuntime()
    {
        JPH::RegisterDefaultAllocator();
        JPH::Trace = trace;
        JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = assert_failed;)
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    }
    ~JoltRuntime()
    {
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }
    JoltRuntime(const JoltRuntime&) = delete;
    JoltRuntime& operator=(const JoltRuntime&) = delete;
};

// --- math ---------------------------------------------------------------------------------

// Deterministic pseudo-random floats in [-1, 1).
float next_unit(std::uint64_t& state)
{
    state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    return static_cast<float>(static_cast<double>(state >> 40) / 8388608.0 - 1.0);
}

void test_gaussian_elimination()
{
    // A known 3x3 system: x = (2, 3, -1).
    JPH::Matrix<3, 3> a;
    const float coefficients[3][3] = {{2.F, 1.F, -1.F}, {-3.F, -1.F, 2.F}, {-2.F, 1.F, 2.F}};
    for (JPH::uint r = 0; r < 3; ++r) {
        for (JPH::uint c = 0; c < 3; ++c) {
            a(r, c) = coefficients[r][c];
        }
    }
    JPH::Matrix<3, 1> b;
    b(0, 0) = 8.F;
    b(1, 0) = -11.F;
    b(2, 0) = -3.F;
    expect(JPH::GaussianElimination(a, b), "a regular 3x3 system solves");
    expect(close_to(b(0, 0), 2.F, 1e-5F) && close_to(b(1, 0), 3.F, 1e-5F) && close_to(b(2, 0), -1.F, 1e-5F),
           "the 3x3 solution is (2, 3, -1)");

    // A permutation matrix: every natural pivot is zero, so each column needs a row swap.
    // (2x2 matrices are inverted by a closed formula in Jolt, so these cases are 3x3 and 4x4.)
    JPH::Matrix<3, 3> permutation = JPH::Matrix<3, 3>::sZero();
    permutation(0, 1) = 1.F;
    permutation(1, 2) = 1.F;
    permutation(2, 0) = 1.F;
    JPH::Matrix<3, 3> solved = JPH::Matrix<3, 3>::sIdentity();
    JPH::Matrix<3, 3> work = permutation;
    expect(JPH::GaussianElimination(work, solved), "a matrix with zero pivots solves after row swaps");
    bool transpose = true;
    for (JPH::uint r = 0; r < 3; ++r) {
        for (JPH::uint c = 0; c < 3; ++c) {
            transpose = transpose && solved(r, c) == permutation(c, r);
        }
    }
    expect(transpose, "the inverse of a permutation matrix is its transpose");

    // A singular matrix (row 2 is twice row 1) is refused.
    JPH::Matrix<3, 3> singular;
    const float rows[3][3] = {{1.F, 2.F, 3.F}, {2.F, 4.F, 6.F}, {1.F, 0.F, 1.F}};
    for (JPH::uint r = 0; r < 3; ++r) {
        for (JPH::uint c = 0; c < 3; ++c) {
            singular(r, c) = rows[r][c];
        }
    }
    JPH::Matrix<3, 3> unused = JPH::Matrix<3, 3>::sIdentity();
    expect(!JPH::GaussianElimination(singular, unused), "a singular matrix is refused");

    // Random well-conditioned 4x4 matrices: M * inverse(M) is the identity.
    std::uint64_t state = 0x6c796b6f;
    for (int trial = 0; trial < 50; ++trial) {
        JPH::Matrix<4, 4> m;
        for (JPH::uint r = 0; r < 4; ++r) {
            for (JPH::uint c = 0; c < 4; ++c) {
                // Diagonally dominant, then rows reversed: well conditioned, but the natural
                // pivots are small, so the solver must swap rows.
                m(3 - r, c) = next_unit(state) + (r == c ? 4.F : 0.F);
            }
        }
        JPH::Matrix<4, 4> inverse;
        expect(inverse.SetInversed(m), "a random matrix with reversed rows inverts");
        float worst = 0.F;
        for (JPH::uint r = 0; r < 4; ++r) {
            for (JPH::uint c = 0; c < 4; ++c) {
                float sum = 0.F;
                for (JPH::uint k = 0; k < 4; ++k) {
                    sum += m(r, k) * inverse(k, c);
                }
                worst = std::fmax(worst, std::fabs(sum - (r == c ? 1.F : 0.F)));
            }
        }
        expect(worst < 1e-5F, "M * inverse(M) is the identity");
    }
}

template <JPH::uint N>
void check_eigen(const JPH::Matrix<N, N>& a, std::string_view what)
{
    JPH::Matrix<N, N> vectors = JPH::Matrix<N, N>::sIdentity();
    JPH::Vector<N> values;
    if (!JPH::EigenValueSymmetric(a, vectors, values)) {
        expect(false, std::string{what} + ": converges");
        return;
    }
    float scale = 0.F;
    for (JPH::uint r = 0; r < N; ++r) {
        for (JPH::uint c = 0; c < N; ++c) {
            scale = std::fmax(scale, std::fabs(a(r, c)));
        }
    }
    float reconstruction = 0.F;
    float orthonormality = 0.F;
    for (JPH::uint r = 0; r < N; ++r) {
        for (JPH::uint c = 0; c < N; ++c) {
            float rebuilt = 0.F; // (V diag(values) V^T)(r, c)
            float dot = 0.F;     // (V^T V)(r, c)
            for (JPH::uint k = 0; k < N; ++k) {
                rebuilt += vectors(r, k) * values[k] * vectors(c, k);
                dot += vectors(k, r) * vectors(k, c);
            }
            reconstruction = std::fmax(reconstruction, std::fabs(rebuilt - a(r, c)));
            orthonormality = std::fmax(orthonormality, std::fabs(dot - (r == c ? 1.F : 0.F)));
        }
    }
    expect(reconstruction <= 1e-5F * std::fmax(scale, 1.F), std::string{what} + ": V diag(values) V^T rebuilds A");
    expect(orthonormality <= 1e-5F, std::string{what} + ": the eigenvectors are orthonormal");
}

void test_eigen_values()
{
    // [[2, 1], [1, 2]] has eigenvalues 1 and 3.
    JPH::Matrix<2, 2> two;
    two(0, 0) = 2.F;
    two(0, 1) = 1.F;
    two(1, 0) = 1.F;
    two(1, 1) = 2.F;
    JPH::Matrix<2, 2> vectors = JPH::Matrix<2, 2>::sIdentity();
    JPH::Vector<2> values;
    expect(JPH::EigenValueSymmetric(two, vectors, values), "the 2x2 example converges");
    const float low = std::fmin(values[0], values[1]);
    const float high = std::fmax(values[0], values[1]);
    expect(close_to(low, 1.F, 1e-6F) && close_to(high, 3.F, 1e-6F), "the 2x2 eigenvalues are 1 and 3");

    // Already diagonal, and all zero.
    JPH::Matrix<3, 3> diagonal = JPH::Matrix<3, 3>::sZero();
    diagonal(0, 0) = 5.F;
    diagonal(1, 1) = -2.F;
    diagonal(2, 2) = 7.F;
    check_eigen(diagonal, "a diagonal matrix");
    check_eigen(JPH::Matrix<3, 3>::sZero(), "the zero matrix");

    // Random symmetric 3x3 and 4x4 matrices, including repeated eigenvalues.
    std::uint64_t state = 42;
    for (int trial = 0; trial < 100; ++trial) {
        JPH::Matrix<3, 3> m3;
        JPH::Matrix<4, 4> m4;
        for (JPH::uint r = 0; r < 4; ++r) {
            for (JPH::uint c = r; c < 4; ++c) {
                const float value = next_unit(state) * 10.F;
                m4(r, c) = value;
                m4(c, r) = value;
                if (r < 3 && c < 3) {
                    m3(r, c) = value;
                    m3(c, r) = value;
                }
            }
        }
        check_eigen(m3, "a random symmetric 3x3 matrix");
        check_eigen(m4, "a random symmetric 4x4 matrix");
    }
    JPH::Matrix<3, 3> repeated = JPH::Matrix<3, 3>::sIdentity();
    repeated(0, 1) = 1.F;
    repeated(1, 0) = 1.F;
    check_eigen(repeated, "a matrix with a repeated eigenvalue");
}

void test_inertia()
{
    // A 2 x 4 x 6 m box: principal moments are m/12 * (4^2 + 6^2), m/12 * (2^2 + 6^2), m/12 * (2^2 + 4^2).
    JPH::BoxShapeSettings settings{JPH::Vec3(1.F, 2.F, 3.F)};
    settings.SetDensity(1.F);
    const JPH::ShapeSettings::ShapeResult result = settings.Create();
    expect(result.IsValid(), "the box shape is created");
    if (!result.IsValid()) {
        return;
    }
    const JPH::MassProperties box = result.Get()->GetMassProperties();
    const float mass = box.mMass;
    expect(close_to(mass, 48.F, 1e-3F), "the box's mass is its volume at density 1");
    const float expected[3] = {mass / 12.F * 52.F, mass / 12.F * 40.F, mass / 12.F * 20.F};

    // Rotate the inertia tensor; the decomposition must recover the principal moments.
    const JPH::Mat44 rotation = JPH::Mat44::sRotation(JPH::Vec3(1.F, 2.F, 3.F).Normalized(), 0.7F);
    JPH::MassProperties rotated = box;
    rotated.mInertia = rotation * box.mInertia * rotation.Transposed();
    JPH::Mat44 principal_axes;
    JPH::Vec3 moments;
    expect(rotated.DecomposePrincipalMomentsOfInertia(principal_axes, moments), "the rotated inertia decomposes");
    for (int i = 0; i < 3; ++i) {
        expect(close_to(moments[i], expected[i], 1e-3F * expected[i]),
               "principal moment " + std::to_string(i) + " matches the box, largest first");
    }
}

int run_math()
{
    test_gaussian_elimination();
    test_eigen_values();
    test_inertia();
    return g_failures;
}

// --- determinism --------------------------------------------------------------------------

namespace layer {
constexpr JPH::ObjectLayer static_objects = 0;
constexpr JPH::ObjectLayer moving_objects = 1;
constexpr JPH::uint count = 2;
}

class ObjectPairs final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
    {
        return a == layer::moving_objects || b == layer::moving_objects;
    }
};

class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return layer::count; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer object_layer) const override
    {
        return JPH::BroadPhaseLayer(static_cast<JPH::BroadPhaseLayer::Type>(object_layer));
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer) const override { return "layer"; }
#endif
};

class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer object_layer, JPH::BroadPhaseLayer broad_phase) const override
    {
        return object_layer == layer::moving_objects
            || broad_phase == JPH::BroadPhaseLayer(static_cast<JPH::BroadPhaseLayer::Type>(layer::moving_objects));
    }
};

// FNV-1a over the exact bits of every float and double.
class StateHash {
public:
    void add(float value)
    {
        const auto bits = std::bit_cast<std::uint32_t>(value);
        for (int shift = 0; shift < 32; shift += 8) {
            hash_ = (hash_ ^ ((bits >> shift) & 0xFFU)) * 0x100000001b3ULL;
        }
    }
    void add(double value)
    {
        const auto bits = std::bit_cast<std::uint64_t>(value);
        for (int shift = 0; shift < 64; shift += 8) {
            hash_ = (hash_ ^ ((bits >> shift) & 0xFFU)) * 0x100000001b3ULL;
        }
    }
    [[nodiscard]] std::uint64_t value() const { return hash_; }

private:
    std::uint64_t hash_ = 0xcbf29ce484222325ULL;
};

// The value every build must produce (cross-platform determinism, ADR 0003). Changed from
// 0x17e8264fc6fa9cd7 when positions became double precision (ADR 0022).
constexpr std::uint64_t expected_hash = 0x3ae5ee6746df5badULL;

constexpr int total_ticks = 300;
constexpr int save_tick = 150;

class Scene {
public:
    Scene()
        : temp_allocator_(10 * 1024 * 1024)
        , jobs_(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, 2)
    {
        system_.Init(256, 0, 1024, 1024, broad_phase_layers_, object_vs_broad_phase_, object_pairs_);
        JPH::BodyInterface& bodies = system_.GetBodyInterface();

        JPH::BodyCreationSettings ground{new JPH::BoxShape(JPH::Vec3(50.F, 1.F, 50.F)), JPH::RVec3(0.F, -1.F, 0.F),
                                         JPH::Quat::sIdentity(), JPH::EMotionType::Static, layer::static_objects};
        ground_ = bodies.CreateAndAddBody(ground, JPH::EActivation::DontActivate);

        // A 4 x 4 x 4 block of alternating boxes and spheres, slightly spun so it collapses.
        for (int x = 0; x < 4; ++x) {
            for (int y = 0; y < 4; ++y) {
                for (int z = 0; z < 4; ++z) {
                    // The settings take the only reference to the new shape; the body adds its own.
                    const bool box = ((x + y + z) % 2) == 0;
                    const JPH::Shape* shape = box ? static_cast<const JPH::Shape*>(new JPH::BoxShape(JPH::Vec3::sReplicate(0.5F)))
                                                  : static_cast<const JPH::Shape*>(new JPH::SphereShape(0.5F));
                    const JPH::RVec3 position{(static_cast<float>(x) - 1.5F) * 1.1F, 2.F + static_cast<float>(y) * 1.1F,
                                              (static_cast<float>(z) - 1.5F) * 1.1F};
                    JPH::BodyCreationSettings body{shape, position, JPH::Quat::sIdentity(), JPH::EMotionType::Dynamic,
                                                   layer::moving_objects};
                    body.mAngularVelocity = JPH::Vec3(0.3F * static_cast<float>(x - 1), 0.2F, 0.1F * static_cast<float>(z));
                    dynamic_.push_back(bodies.CreateAndAddBody(body, JPH::EActivation::Activate));
                }
            }
        }
        system_.OptimizeBroadPhase();
    }

    ~Scene()
    {
        JPH::BodyInterface& bodies = system_.GetBodyInterface();
        for (const JPH::BodyID id : dynamic_) {
            bodies.RemoveBody(id);
            bodies.DestroyBody(id);
        }
        bodies.RemoveBody(ground_);
        bodies.DestroyBody(ground_);
    }

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    bool step(float tick_seconds)
    {
        return system_.Update(tick_seconds, 1, &temp_allocator_, &jobs_) == JPH::EPhysicsUpdateError::None;
    }

    [[nodiscard]] std::uint64_t hash() const
    {
        const JPH::BodyInterface& bodies = system_.GetBodyInterface();
        StateHash hash;
        for (const JPH::BodyID id : dynamic_) {
            JPH::RVec3 position;
            JPH::Quat rotation;
            bodies.GetPositionAndRotation(id, position, rotation);
            const JPH::Vec3 linear = bodies.GetLinearVelocity(id);
            const JPH::Vec3 angular = bodies.GetAngularVelocity(id);
            for (int i = 0; i < 3; ++i) {
                hash.add(static_cast<double>(position[i])); // all 64 bits (ADR 0022)
                hash.add(linear[i]);
                hash.add(angular[i]);
            }
            hash.add(rotation.GetX());
            hash.add(rotation.GetY());
            hash.add(rotation.GetZ());
            hash.add(rotation.GetW());
        }
        return hash.value();
    }

    [[nodiscard]] float lowest_centre() const
    {
        const JPH::BodyInterface& bodies = system_.GetBodyInterface();
        float lowest = 1e30F;
        for (const JPH::BodyID id : dynamic_) {
            lowest = std::fmin(lowest, static_cast<float>(bodies.GetCenterOfMassPosition(id).GetY()));
        }
        return lowest;
    }

    void save(JPH::StateRecorderImpl& recorder) const { system_.SaveState(recorder); }
    bool restore(JPH::StateRecorderImpl& recorder) { return system_.RestoreState(recorder); }

private:
    JPH::TempAllocatorImpl temp_allocator_;
    JPH::JobSystemThreadPool jobs_;
    BroadPhaseLayers broad_phase_layers_;
    ObjectVsBroadPhase object_vs_broad_phase_;
    ObjectPairs object_pairs_;
    JPH::PhysicsSystem system_;
    JPH::BodyID ground_;
    std::vector<JPH::BodyID> dynamic_;
};

std::string hex(std::uint64_t value)
{
    char text[19];
    std::snprintf(text, sizeof(text), "0x%016llx", static_cast<unsigned long long>(value));
    return text;
}

int run_determinism()
{
    // One physics step per simulation tick (ADR 0017).
    std::string clock_error;
    const auto clock = lykoware::FixedStep::create({}, clock_error);
    expect(clock.has_value(), "the default simulation clock is valid");
    if (!clock) {
        return g_failures;
    }
    const auto tick_seconds = static_cast<float>(clock->tick_seconds());

    std::uint64_t first_hash = 0;
    {
        Scene scene;
        bool stepped = true;
        for (int tick = 0; tick < total_ticks; ++tick) {
            stepped = scene.step(tick_seconds) && stepped;
        }
        expect(stepped, "every physics step succeeds");
        first_hash = scene.hash();
        expect(scene.lowest_centre() > 0.3F, "no body falls through the ground");
    }

    std::uint64_t second_hash = 0;
    std::uint64_t restored_hash = 0;
    {
        Scene scene;
        JPH::StateRecorderImpl recorder;
        for (int tick = 0; tick < total_ticks; ++tick) {
            if (tick == save_tick) {
                scene.save(recorder);
            }
            (void)scene.step(tick_seconds);
        }
        second_hash = scene.hash();

        recorder.Rewind();
        expect(scene.restore(recorder), "the saved state restores");
        for (int tick = save_tick; tick < total_ticks; ++tick) {
            (void)scene.step(tick_seconds);
        }
        restored_hash = scene.hash();
    }

    std::cout << "state hash after " << total_ticks << " ticks: " << hex(first_hash) << '\n';
    expect(first_hash == second_hash, "two runs of the same scene give the same state");
    expect(restored_hash == first_hash, "restoring a saved state and re-running reproduces the state");
    expect(first_hash == expected_hash,
           "the state hash matches the value every platform must produce (" + hex(expected_hash) + ")");
    return g_failures;
}

}

int main(int argc, char* argv[])
{
    const std::string_view mode = argc == 2 ? std::string_view{argv[1]} : std::string_view{};
    if (mode != "math" && mode != "determinism") {
        std::cerr << "usage: lykoware_physics_jolt_test math | determinism\n";
        return EXIT_FAILURE;
    }
    {
        const JoltRuntime runtime;
        if (mode == "math") {
            (void)run_math();
        } else {
            (void)run_determinism();
        }
    }
    expect(g_jolt_asserts == 0, "Jolt raised no asserts");
    if (g_failures != 0) {
        std::cerr << g_failures << " physics assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "physics " << mode << " passed\n";
    return EXIT_SUCCESS;
}
