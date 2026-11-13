// animation.ik_runtime_scale (02.11b-runtime-scale): poses representative rigs and a crowd through
// the prepared two-bone solver (ADR 0061) every frame, with each character's skinning palette, and
// reports heap allocations and time per solve. CI runs a small crowd and holds correctness and the
// allocation budget; timing is printed, never asserted here. The target-PC run is the owner's.
//
// Usage: lykoware_ik_runtime_scale [--characters N] [--frames F] [--csv PATH]
// CSV columns (one row per rig):
//   rig,joints,characters,frames,solves,allocations,mean_ns_per_solve,worst_frame_ms,mean_palette_ns

#include <lykoware/animation.hpp>
#include <lykoware/ik.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <new>
#include <optional>
#include <string>
#include <vector>

namespace {
thread_local bool count_allocations = false;
thread_local std::size_t allocations = 0;
}

void* operator new(std::size_t size)
{
    if (count_allocations) { ++allocations; }
    if (void* result = std::malloc(size == 0 ? 1 : size)) { return result; }
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

namespace {

using namespace lykoware;

int failures = 0;
void expect(bool ok, const std::string& why)
{
    if (!ok) {
        std::cerr << "FAIL: " << why << '\n';
        ++failures;
    }
}

// Allocations the measured frames may make, for the whole run. The prepared path and a retained
// palette make none; the budget is zero.
constexpr std::size_t allocation_budget = 0;
constexpr double reach_tolerance = 5e-4; // metres between the solved foot and a reachable target

struct Leg {
    std::size_t hip = 0, knee = 0, foot = 0;
};

struct Rig {
    std::string name;
    animation::Skeleton skeleton;
    std::vector<Leg> legs;
};

// A body with a spine of `spine` joints, two arms of `arm` joints and two three-joint legs, legs
// hanging 0.45 + 0.45 m below a hip at `hip_height`.
Rig make_rig(std::string name, std::size_t spine, std::size_t arm, float hip_height)
{
    Rig rig;
    rig.name = std::move(name);
    auto& joints = rig.skeleton.joints;
    const auto add = [&](std::optional<std::uint16_t> parent, math::Vec3 offset) {
        animation::Joint joint;
        joint.parent = parent;
        joint.rest.translation = offset;
        joints.push_back(joint);
        return static_cast<std::uint16_t>(joints.size() - 1);
    };
    const auto root = add(std::nullopt, {0.F, hip_height, 0.F});
    auto top = root;
    for (std::size_t i = 0; i < spine; ++i) { top = add(top, {0.F, .5F / static_cast<float>(spine), 0.F}); }
    for (const float side : {-1.F, 1.F}) {
        auto hand = add(top, {side * .2F, 0.F, 0.F});
        for (std::size_t i = 0; i < arm; ++i) { hand = add(hand, {side * .5F / static_cast<float>(arm), 0.F, 0.F}); }
        const auto hip = add(root, {side * .12F, 0.F, 0.F});
        const auto knee = add(hip, {0.F, -.45F, .01F});
        const auto foot = add(knee, {0.F, -.45F, -.01F});
        rig.legs.push_back({hip, knee, foot});
    }
    return rig;
}

struct Result {
    std::size_t solves = 0;
    std::size_t allocations = 0;
    double mean_ns_per_solve = 0.0;
    double worst_frame_ms = 0.0;
    double mean_palette_ns = 0.0;
    double worst_reach = 0.0;
    bool all_solved = true;
};

// Where character `c`'s leg `l` should put its foot on frame `f`, in model space: a stepping
// pattern that is always reachable (well inside 0.9 m of the hip).
math::DVec3 foot_target(const Rig& rig, const animation::Pose& rest, std::size_t c, std::size_t l, std::size_t f)
{
    const auto& hip = rest[rig.legs[l].hip].translation;
    const double phase = static_cast<double>(f) * .09 + static_cast<double>(c) * .37 + static_cast<double>(l) * 3.14159;
    return {static_cast<double>(hip.x), static_cast<double>(rest[0].translation.y) - .72 + .08 * std::sin(phase),
            .25 * std::cos(phase)};
}

Result run(const Rig& rig, std::size_t characters, std::size_t frames, bool corrupt_one)
{
    Result result;
    std::string error;
    std::vector<animation::PreparedTwoBone> plans;
    for (const auto& leg : rig.legs) {
        auto plan = animation::PreparedTwoBone::prepare(rig.skeleton, leg.hip, leg.knee, leg.foot, error);
        expect(plan.has_value(), rig.name + ": prepare leg: " + error);
        if (!plan) { result.all_solved = false; return result; }
        plans.push_back(*plan);
    }
    const auto rest = animation::rest_pose(rig.skeleton);
    // Retained per-character storage, as a crowd system would hold it.
    std::vector<animation::Pose> poses(characters, rest);
    std::vector<std::vector<math::Mat4>> palettes(characters);
    std::vector<std::vector<math::Mat4>> globals(characters);
    for (std::size_t c = 0; c < characters; ++c) {
        animation::skinning_matrices(rig.skeleton, poses[c], palettes[c]);
        animation::global_transforms(rig.skeleton, poses[c], globals[c]);
    }
    // Targets are computed before measuring so the window holds only solving and palettes.
    std::vector<math::DVec3> targets(frames * characters * rig.legs.size());
    for (std::size_t f = 0; f < frames; ++f) {
        for (std::size_t c = 0; c < characters; ++c) {
            for (std::size_t l = 0; l < rig.legs.size(); ++l) {
                targets[(f * characters + c) * rig.legs.size() + l] = foot_target(rig, rest, c, l, f);
            }
        }
    }
    animation::TwoBoneScratch scratch;
    animation::TwoBoneSolution solution;
    const math::DVec3 bend{0.0, 0.0, -1.0}; // knees forward (-Z)
    double solve_ns = 0.0;
    double palette_ns = 0.0;
    using clock = std::chrono::steady_clock;

    allocations = 0;
    count_allocations = true;
    for (std::size_t f = 0; f < frames; ++f) {
        const auto frame_start = clock::now();
        for (std::size_t c = 0; c < characters; ++c) {
            auto& pose = poses[c];
            const auto solve_start = clock::now();
            for (std::size_t l = 0; l < plans.size(); ++l) {
                const auto& target = targets[(f * characters + c) * rig.legs.size() + l];
                if (!plans[l].apply(pose, target, bend, scratch, pose, solution, error)) {
                    result.all_solved = false;
                    continue;
                }
                ++result.solves;
                result.worst_reach = std::max(result.worst_reach,
                    std::hypot(solution.end.x - target.x, solution.end.y - target.y, solution.end.z - target.z));
            }
            const auto palette_start = clock::now();
            if (corrupt_one && f == frames / 2 && c == 0) {
                // Deliberate break: a wrong pose (the knee straightened after solving).
                pose[rig.legs[0].knee].rotation = {0.F, 0.F, 0.F, 1.F};
            }
            animation::skinning_matrices(rig.skeleton, pose, palettes[c]);
            const auto palette_end = clock::now();
            solve_ns += std::chrono::duration<double, std::nano>(palette_start - solve_start).count();
            palette_ns += std::chrono::duration<double, std::nano>(palette_end - palette_start).count();
        }
        const double frame_ms = std::chrono::duration<double, std::milli>(clock::now() - frame_start).count();
        result.worst_frame_ms = std::max(result.worst_frame_ms, frame_ms);
    }
    count_allocations = false;
    result.allocations = allocations;

    // Independent check of the final poses: forward kinematics puts each foot on its last target.
    for (std::size_t c = 0; c < characters; ++c) {
        animation::global_transforms(rig.skeleton, poses[c], globals[c]);
        for (std::size_t l = 0; l < rig.legs.size(); ++l) {
            const auto& m = globals[c][rig.legs[l].foot];
            const auto& target = targets[((frames - 1) * characters + c) * rig.legs.size() + l];
            result.worst_reach = std::max(result.worst_reach,
                std::hypot(m.m[3][0] - target.x, m.m[3][1] - target.y, m.m[3][2] - target.z));
        }
    }
    result.mean_ns_per_solve = result.solves > 0 ? solve_ns / static_cast<double>(result.solves) : 0.0;
    result.mean_palette_ns = characters * frames > 0 ? palette_ns / static_cast<double>(characters * frames) : 0.0;
    return result;
}

std::size_t parse_count(const std::string& text, std::size_t low, std::size_t high, const std::string& name)
{
    char* end = nullptr;
    const auto value = std::strtoull(text.c_str(), &end, 10);
    if (text.empty() || end == nullptr || *end != '\0' || value < low || value > high) {
        throw std::invalid_argument(name + " must be " + std::to_string(low) + ".." + std::to_string(high));
    }
    return static_cast<std::size_t>(value);
}

} // namespace

int main(int argc, char** argv)
{
    std::size_t characters = 32;
    std::size_t frames = 60;
    std::string csv;
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string key = argv[i];
            if (i + 1 >= argc) { throw std::invalid_argument(key + " needs a value"); }
            const std::string value = argv[++i];
            if (key == "--characters") { characters = parse_count(value, 1, 10000, key); }
            else if (key == "--frames") { frames = parse_count(value, 2, 100000, key); }
            else if (key == "--csv") { csv = value; }
            else { throw std::invalid_argument("unknown option " + key); }
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 2;
    }

    const std::vector<Rig> rigs{
        make_rig("mannequin", 1, 1, .95F),     // the course mannequin's size
        make_rig("humanoid", 6, 8, 1.F),       // a typical imported humanoid with fingers folded in
        make_rig("dense", 40, 100, 1.05F),      // near max_joints (256): the worst case a rig may be
    };

    std::ofstream file;
    if (!csv.empty()) {
        file.open(csv, std::ios::binary);
        if (!file) { std::cerr << "cannot write " << csv << '\n'; return 2; }
    }
    const std::string header = "rig,joints,characters,frames,solves,allocations,mean_ns_per_solve,worst_frame_ms,mean_palette_ns";
    std::cout << header << '\n';
    if (file) { file << header << '\n'; }
    for (const auto& rig : rigs) {
        expect(rig.skeleton.joints.size() <= animation::max_joints, rig.name + " fits max_joints");
        const auto r = run(rig, characters, frames, false);
        const std::string row = rig.name + ',' + std::to_string(rig.skeleton.joints.size()) + ',' + std::to_string(characters)
            + ',' + std::to_string(frames) + ',' + std::to_string(r.solves) + ',' + std::to_string(r.allocations) + ','
            + std::to_string(r.mean_ns_per_solve) + ',' + std::to_string(r.worst_frame_ms) + ',' + std::to_string(r.mean_palette_ns);
        std::cout << row << '\n';
        if (file) { file << row << '\n'; }
        expect(r.all_solved && r.solves == characters * frames * rig.legs.size(), rig.name + ": every leg solves every frame");
        expect(r.worst_reach < reach_tolerance, rig.name + ": every foot reaches its target (FK check)");
        expect(r.allocations <= allocation_budget, rig.name + ": measured frames stay within the allocation budget");
    }

    // Deliberate breaks prove each gate can fail.
    {
        // A wrong pose is caught by the FK check: straightening one knee after the last solve moves
        // the foot off target.
        const auto r = run(rigs[0], 2, 2, true);
        expect(r.worst_reach > reach_tolerance, "a wrong pose fails the reach check");
    }
    {
        // The counter sees allocations: the reference (unprepared) solver allocates per call.
        const auto& rig = rigs[0];
        const auto rest = animation::rest_pose(rig.skeleton);
        animation::Pose out;
        animation::TwoBoneSolution solution;
        std::string error;
        allocations = 0;
        count_allocations = true;
        for (int i = 0; i < 4; ++i) {
            (void)animation::pose_two_bone(rig.skeleton, rest, rig.legs[0].hip, rig.legs[0].knee, rig.legs[0].foot,
                                           foot_target(rig, rest, 0, 0, static_cast<std::size_t>(i)), {0, 0, -1}, out, solution, error);
        }
        count_allocations = false;
        expect(allocations > allocation_budget, "the reference solver's allocations exceed the budget, so the counter is live");
    }

    return failures == 0 ? 0 : 1;
}
