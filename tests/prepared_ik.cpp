#include <lykoware/ik.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
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
bool same(const animation::Pose& a, const animation::Pose& b)
{
    if (a.size() != b.size()) { return false; }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].translation != b[i].translation || a[i].scale != b[i].scale
            || a[i].rotation.x != b[i].rotation.x || a[i].rotation.y != b[i].rotation.y
            || a[i].rotation.z != b[i].rotation.z || a[i].rotation.w != b[i].rotation.w) { return false; }
    }
    return true;
}
}
int main()
{
    using namespace lykoware;
    int failures = 0;
    const auto expect = [&](bool ok, const char* why) {
        if (!ok) { std::cerr << "FAIL: " << why << '\n'; ++failures; }
    };
    animation::Skeleton rig;
    rig.joints.resize(5);
    rig.joints[0].rest.translation = {4, 1, 0};
    rig.joints[0].rest.rotation = math::from_axis_angle({0, 0, 1}, 0.7F);
    rig.joints[1].parent = std::uint16_t{0}; rig.joints[1].rest.translation = {.2F, .1F, 0};
    rig.joints[2].parent = std::uint16_t{1}; rig.joints[2].rest.translation = {1, 0, 0};
    rig.joints[3].parent = std::uint16_t{2}; rig.joints[3].rest.translation = {1, 0, 0};
    rig.joints[4].parent = std::uint16_t{0};
    std::string error;
    auto plan = animation::PreparedTwoBone::prepare(rig, 1, 2, 3, error);
    expect(plan.has_value(), "prepare parented chain");
    if (!plan) { return 1; }
    expect(plan->joint_count() == 5, "snapshot joint count");
    animation::TwoBoneScratch scratch;
    const auto rest = animation::rest_pose(rig);
    animation::Pose output(rest.size()), reference;
    animation::TwoBoneSolution solved, expected;
    std::vector<math::Mat4> fk;
    for (int i = 0; i < 128; ++i) {
        auto input = rest;
        input[0].rotation = math::from_axis_angle({0, 1, 0}, i*.017F);
        input[1].rotation = math::from_axis_angle({0, 0, 1}, i*.011F);
        const math::DVec3 target{4 + std::cos(i*.13)*2.5, 1 + std::sin(i*.19)*2, std::sin(i*.07)};
        const math::DVec3 pole{4, 3, 1};
        expect(animation::pose_two_bone(rig, input, 1, 2, 3, target, pole, reference, expected, error), "reference varied target");
        expect(plan->apply(input, target, pole, scratch, output, solved, error), "prepared varied target");
        expect(same(output, reference) && solved.end == expected.end && solved.knee == expected.knee
            && solved.target_clamped == expected.target_clamped, "prepared/reference poses and flags agree exactly");
        animation::global_transforms(rig, output, fk);
        const auto& matrix = fk[3];
        expect(std::hypot(matrix.m[3][0] - solved.end.x, matrix.m[3][1] - solved.end.y,
                          matrix.m[3][2] - solved.end.z) < 5e-4, "independent FK agrees with solved endpoint");
        expect(output[0].rotation.w == input[0].rotation.w && output[4].rotation.w == input[4].rotation.w
            && output[3].rotation.w == input[3].rotation.w && output[2].translation == input[2].translation,
            "ancestor/sibling/end/translation preserved");
    }
    auto alias = rest;
    expect(plan->apply(alias, {4.5, 2.2, .3}, {4, 3, 1}, scratch, alias, solved, error), "input/output alias supported");
    const auto before = output;
    const auto old_solution = solved;
    const auto unchanged = [&] { return same(output, before) && solved.end == old_solution.end && solved.knee == old_solution.knee; };
    auto bad = rest; bad[2].translation = {2, 0, 0};
    expect(!plan->apply(bad, {}, {}, scratch, output, solved, error) && unchanged(), "changed limb fails transactionally");
    for (int kind = 0; kind < 4; ++kind) {
        bad = rest;
        if (kind == 0) { bad[4].translation.x = std::numeric_limits<float>::infinity(); }
        if (kind == 1) { bad[4].scale = {2, 1, 1}; }
        if (kind == 2) { bad[4].rotation = {0, 0, 0, 0}; }
        if (kind == 3) { bad.pop_back(); }
        expect(!plan->apply(bad, {}, {}, scratch, output, solved, error) && unchanged(), "invalid dynamic pose leaves public output unchanged");
    }
    expect(!plan->apply(rest, {std::numeric_limits<double>::quiet_NaN(), 0, 0}, {}, scratch, output, solved, error)
        && unchanged(), "invalid target leaves outputs unchanged");
    expect(!plan->apply(rest, {}, {}, scratch, std::span(output).first(4), solved, error) && unchanged(), "output size mismatch unchanged");
    auto invalid = rig; invalid.joints[2].parent = std::uint16_t{0};
    expect(!animation::PreparedTwoBone::prepare(invalid, 1, 2, 3, error), "helper/non-direct chain rejected");
    invalid = rig; invalid.joints[2].rest.translation = {};
    expect(!animation::PreparedTwoBone::prepare(invalid, 1, 2, 3, error), "collapsed rest rejected");
    invalid = rig; invalid.joints[4].rest.scale = {2, 1, 1};
    expect(!animation::PreparedTwoBone::prepare(invalid, 1, 2, 3, error), "nonunit rest rejected");
    invalid = rig; invalid.joints[0].parent = std::uint16_t{4};
    expect(!animation::PreparedTwoBone::prepare(invalid, 1, 2, 3, error), "bad hierarchy rejected");
    expect(!animation::PreparedTwoBone::prepare(rig, 1, 2, 999, error), "out-of-range role rejected");
    animation::Skeleton simple;
    simple.joints.resize(3);
    simple.joints[1].parent = std::uint16_t{0}; simple.joints[1].rest.translation = {1, 0, 0};
    simple.joints[2].parent = std::uint16_t{1}; simple.joints[2].rest.translation = {1, 0, 0};
    const auto simple_rest = animation::rest_pose(simple);
    const auto simple_plan = animation::PreparedTwoBone::prepare(simple, 0, 1, 2, error);
    expect(simple_plan.has_value(), "root chain preparation");
    if (!simple_plan) { return 1; }
    animation::Pose simple_out(3);
    for (const math::DVec3 target : {math::DVec3{-1.999999, .001, 0}, {-2, 0, 0}, {0, 0, 0}, {5, 0, 0}, {1, 0, 0}}) {
        expect(animation::pose_two_bone(simple, simple_rest, 0, 1, 2, target, {2, 0, 0}, reference, expected, error)
            && simple_plan->apply(simple_rest, target, {2, 0, 0}, scratch, simple_out, solved, error)
            && same(simple_out, reference) && solved.used_fallback_direction == expected.used_fallback_direction
            && solved.used_fallback_bend == expected.used_fallback_bend && solved.target_clamped == expected.target_clamped,
            "antiparallel, coincident, collinear and unreachable reference equivalence");
    }
    animation::Pose overlap(4);
    std::copy(simple_rest.begin(), simple_rest.end(), overlap.begin());
    expect(simple_plan->apply(std::span(overlap).first(3), {1, 1, 0}, {0, 2, 0}, scratch,
        std::span(overlap).subspan(1), solved, error), "partially overlapping spans supported");
    animation::Skeleton maximum;
    maximum.joints.resize(animation::max_joints);
    maximum.joints[1].parent = std::uint16_t{0}; maximum.joints[1].rest.translation = {1, 0, 0};
    maximum.joints.back().parent = std::uint16_t{1}; maximum.joints.back().rest.translation = {1, 0, 0};
    const auto maximum_plan = animation::PreparedTwoBone::prepare(maximum, 0, 1, animation::max_joints - 1, error);
    const auto maximum_rest = animation::rest_pose(maximum);
    animation::Pose maximum_out(animation::max_joints);
    expect(maximum_plan && maximum_plan->apply(maximum_rest, {1, 1, 0}, {0, 2, 0}, scratch, maximum_out, solved, error),
        "maximum joint count and highest joint index supported with reused scratch");
    maximum.joints.emplace_back();
    expect(!animation::PreparedTwoBone::prepare(maximum, 0, 1, 2, error), "oversized skeleton rejected");
    // Positive control: reference allocations prove that the interception is active.
    allocations = 0; count_allocations = true;
    const auto reference_start = std::chrono::steady_clock::now();
    bool reference_ok = true;
    for (int i = 0; i < 1000; ++i) {
        reference_ok = animation::pose_two_bone(rig, rest, 1, 2, 3, {4.5, 2.2, .3}, {4, 3, 1}, reference, expected, error) && reference_ok;
    }
    const auto reference_time = std::chrono::steady_clock::now() - reference_start;
    count_allocations = false;
    const auto reference_allocations = allocations;
    expect(reference_ok && reference_allocations > 0, "allocation counter detects reference heap work");
    // Mutation/destruction of the source skeleton must not invalidate the owned plan.
    rig.joints.clear();
    expect(plan->apply(rest, {4.5, 2.2, .3}, {4, 3, 1}, scratch, output, solved, error), "plan survives source skeleton changes");
    bool all_ok = true;
    allocations = 0; count_allocations = true;
    const auto prepared_start = std::chrono::steady_clock::now();
    for (int i = 0; i < 1000; ++i) {
        all_ok = plan->apply(rest, {4.5, 2.2, .3}, {4, 3, 1}, scratch, output, solved, error) && all_ok;
    }
    const auto prepared_time = std::chrono::steady_clock::now() - prepared_start;
    count_allocations = false;
    expect(all_ok && allocations == 0, "1000 successful prepared solves allocate no ordinary C++ heap storage");
    std::cout << "prepared_solves=1000 allocations=" << allocations << '\n';
    std::cout << "reference_allocations=" << reference_allocations
              << " reference_ms=" << std::chrono::duration<double, std::milli>(reference_time).count()
              << " prepared_ms=" << std::chrono::duration<double, std::milli>(prepared_time).count() << '\n';
    return failures == 0 ? 0 : 1;
}
