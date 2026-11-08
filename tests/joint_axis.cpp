#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <lykoware/ik.hpp>
#include <new>
#include <numbers>
namespace {
thread_local bool tracking = false;
thread_local std::size_t allocations = 0;
} // namespace
void* operator new(std::size_t n)
{
    if (tracking) {
        ++allocations;
    }
    if (auto* p = std::malloc(n ? n : 1)) {
        return p;
    }
    throw std::bad_alloc();
}
void* operator new[](std::size_t n)
{
    return ::operator new(n);
}
void operator delete(void* p) noexcept
{
    std::free(p);
}
void operator delete[](void* p) noexcept
{
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept
{
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept
{
    std::free(p);
}
int main()
{
    namespace a = lykoware::animation;
    namespace m = lykoware::math;
    int failures = 0;
    auto check = [&](bool ok, const char* why) {
        if (!ok) {
            std::cerr << "FAIL: " << why << '\n';
            ++failures;
        }
    };
    std::string error;
    a::Skeleton rig;
    a::Joint parent;
    parent.name = "parent";
    parent.rest.rotation = m::from_axis_angle({0, 0, 1}, .4F);
    a::Joint ankle;
    ankle.name = "ankle";
    ankle.parent = std::uint16_t{0};
    ankle.rest.translation = {0, -1, 0};
    ankle.rest.rotation = m::from_axis_angle({1, 0, 0}, .9F);
    a::Joint toe;
    toe.name = "toe";
    toe.parent = std::uint16_t{1};
    toe.rest.translation = {0, 0, -.2F};
    rig.joints = {parent, ankle, toe};
    auto plan = a::PreparedJointAxis::prepare(rig, 1, {0, 1, 0}, error);
    if (!plan) {
        std::cerr << error;
        return 1;
    }
    auto pose = a::rest_pose(rig);
    pose[0].rotation = m::from_axis_angle({0, 1, 0}, .8F);
    auto output = pose;
    const auto rest_global = m::normalize(parent.rest.rotation * ankle.rest.rotation);
    const auto local_up = m::rotate(m::conjugate(rest_global), m::Vec3{0, 1, 0});
    std::vector<m::Mat4> before, after;
    a::global_transforms(rig, pose, before);
    a::TwoBoneScratch scratch;
    a::JointAxisSolution solved;
    const auto target = m::normalize(m::Vec3{.2F, .95F, .1F});
    check(plan->apply(pose, {target.x, target.y, target.z}, std::numbers::pi, scratch, output, solved, error),
          "rotated-parent calibrated axis solve");
    a::global_transforms(rig, output, after);
    const auto up = after[1] * m::Vec4{local_up.x, local_up.y, local_up.z, 0};
    check(m::length(m::Vec3{up.x, up.y, up.z} - target) < 1e-4F,
          "calibrated local axis reaches model normal");
    check(before[1].m[3][0] == after[1].m[3][0] && before[1].m[3][1] == after[1].m[3][1] &&
              before[1].m[3][2] == after[1].m[3][2],
          "ankle origin is unchanged");
    check(output[0].rotation.x == pose[0].rotation.x && output[0].rotation.y == pose[0].rotation.y &&
              output[2].rotation.w == pose[2].rotation.w,
          "only target local rotation changes");
    const auto current = m::normalize(m::rotate(m::normalize(pose[0].rotation * pose[1].rotation), local_up));
    check(plan->apply(pose, {-current.x, -current.y, -current.z}, .1, scratch, output, solved, error) &&
              solved.clamped && std::abs(solved.applied_angle - .1) < 1e-8,
          "angular correction bounded");
    check(std::acos(std::clamp(m::dot(current, solved.model_axis), -1.F, 1.F)) < .1001F,
          "actual result respects angular bound");
    const bool opposite = plan->apply(pose, {-current.x, -current.y, -current.z}, std::numbers::pi, scratch,
                                      output, solved, error);
    if (!opposite || m::length(solved.model_axis + current) >= 1e-4F) {
        std::cerr << "antiparallel: ok=" << opposite << " error=" << error
                  << " difference=" << m::length(solved.model_axis + current)
                  << " angle=" << solved.applied_angle << '\n';
    }
    check(opposite && m::length(solved.model_axis + current) < 1e-4F,
          "antiparallel fallback reaches opposite axis");
    check(plan->apply(pose, {1, 0, 0}, 0, scratch, output, solved, error) &&
              m::length(solved.model_axis - current) < 1e-5F,
          "zero angular budget preserves posed axis");
    auto alias = pose;
    check(plan->apply(alias, {0, 1, 0}, std::numbers::pi, scratch, alias, solved, error),
          "complete alias supported");
    auto overlap = pose;
    overlap.push_back({});
    check(plan->apply(std::span{overlap.data(), 3}, {0, 1, 0}, 1, scratch, std::span{overlap.data() + 1, 3},
                      solved, error),
          "partial overlap supported");
    const auto held = output;
    const auto held_solution = solved;
    auto invalid = pose;
    invalid[1].scale = {2, 1, 1};
    check(!plan->apply(invalid, {0, 1, 0}, 1, scratch, output, solved, error) &&
              output[1].rotation.w == held[1].rotation.w &&
              solved.applied_angle == held_solution.applied_angle,
          "bad scale refuses transactionally");
    for (const auto axis : std::array<m::DVec3, 3>{{{0, 0, 0},
                                                    {0, std::numeric_limits<double>::quiet_NaN(), 0},
                                                    {std::numeric_limits<double>::infinity(), 1, 0}}}) {
        check(!plan->apply(pose, axis, 1, scratch, output, solved, error),
              "degenerate/nonfinite target refused");
    }
    check(!plan->apply(pose, {0, 1, 0}, -1, scratch, output, solved, error) &&
              !plan->apply(pose, {0, 1, 0}, 4, scratch, output, solved, error),
          "invalid angular bounds refused");
    check(!plan->apply(pose, {0, 1, 0}, 1, scratch, std::span{output.data(), 2}, solved, error),
          "incomplete output refused");
    check(!a::PreparedJointAxis::prepare(rig, 99, {0, 1, 0}, error) &&
              !a::PreparedJointAxis::prepare(rig, 1, {0, 0, 0}, error),
          "bad setup refused");
    rig.joints.clear();
    check(plan->apply(pose, {0, 1, 0}, 1, scratch, output, solved, error),
          "owned topology survives source destruction");
    allocations = 0;
    tracking = true;
    auto control = pose;
    tracking = false;
    check(allocations > 0 && control.size() == 3, "allocation control observed");
    allocations = 0;
    tracking = true;
    bool success = true;
    for (int i = 0; i < 1000; ++i) {
        success = plan->apply(pose, {.2, 1, .1}, 1, scratch, output, solved, error) && success;
    }
    tracking = false;
    check(success && allocations == 0,
          "1000 prepared joint-axis applications allocate no ordinary C++ storage");
    std::cout << "axis_updates=1000 allocations=" << allocations << '\n';
    for (std::size_t i = 0; i < a::max_joints; ++i) {
        a::Joint joint;
        joint.name = "joint";
        if (i) {
            joint.parent = static_cast<std::uint16_t>(i - 1);
            joint.rest.translation = {0, .01F, 0};
        }
        rig.joints.push_back(joint);
    }
    auto maximum = a::PreparedJointAxis::prepare(rig, 255, {0, 1, 0}, error);
    pose = a::rest_pose(rig);
    output = pose;
    check(maximum && maximum->apply(pose, {1, 0, 0}, std::numbers::pi, scratch, output, solved, error),
          "maximum 256-joint hierarchy supported");
    return failures ? 1 : 0;
}
