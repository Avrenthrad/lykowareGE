// animation.pose (ADR 0049): skeletons, clip sampling, pose blending and skinning, checked against
// values worked out by hand. No files, window or GPU.

#include <lykoware/animation.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace anim = lykoware::animation;
namespace math = lykoware::math;

constexpr float pi = std::numbers::pi_v<float>;

int g_failures = 0;

void expect(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

bool near(math::Vec3 a, math::Vec3 b, float tolerance = 1e-5F)
{
    return std::abs(a.x - b.x) <= tolerance && std::abs(a.y - b.y) <= tolerance && std::abs(a.z - b.z) <= tolerance;
}

// Two rotations are the same when they turn every vector the same way (q and -q are one rotation).
bool same_rotation(math::Quat a, math::Quat b, float tolerance = 1e-5F)
{
    const math::Vec3 probes[] = {{1.F, 0.F, 0.F}, {0.F, 1.F, 0.F}, {0.F, 0.F, 1.F}};
    for (const math::Vec3 p : probes) {
        if (!near(math::rotate(a, p), math::rotate(b, p), tolerance)) {
            return false;
        }
    }
    return true;
}

std::string text(math::Vec3 v)
{
    return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")";
}

math::Vec4 v3(math::Vec3 v)
{
    return {v.x, v.y, v.z, 0.F};
}

math::Vec4 q4(math::Quat q)
{
    return {q.x, q.y, q.z, q.w};
}

math::Mat4 translation(float x, float y, float z)
{
    return math::translation({x, y, z});
}

// A shoulder at the origin, an elbow 1 m along +X from it and a hand 1 m further, modelled at their
// rest places: each inverse bind matrix undoes its joint's rest place.
anim::Skeleton arm()
{
    anim::Skeleton skeleton;
    skeleton.joints.push_back({"shoulder", std::nullopt, {}, {}});
    skeleton.joints.push_back({"elbow", 0, {{1.F, 0.F, 0.F}, {}, {1.F, 1.F, 1.F}}, translation(-1.F, 0.F, 0.F)});
    skeleton.joints.push_back({"hand", 1, {{1.F, 0.F, 0.F}, {}, {1.F, 1.F, 1.F}}, translation(-2.F, 0.F, 0.F)});
    return skeleton;
}

math::Vec3 skinned(const std::vector<math::Mat4>& matrices, std::array<std::uint16_t, 4> joints, math::Vec4 weights,
                   math::Vec3 point)
{
    return anim::skin_point(matrices, joints, weights, point);
}

void test_skinning()
{
    const anim::Skeleton skeleton = arm();
    std::string error;
    expect(anim::valid_skeleton(skeleton, error), "the arm is a valid skeleton: " + error);
    anim::Pose pose = anim::rest_pose(skeleton);
    std::vector<math::Mat4> matrices;
    anim::skinning_matrices(skeleton, pose, matrices);
    expect(matrices.size() == 3, "one skinning matrix per joint");
    const math::Vec3 tip{2.5F, 0.F, 0.F};
    expect(near(skinned(matrices, {2, 0, 0, 0}, {1.F, 0.F, 0.F, 0.F}, tip), tip),
           "at rest every vertex stays where it was modelled");

    std::vector<math::Mat4> globals;
    pose[0].rotation = math::from_axis_angle({0.F, 0.F, 1.F}, pi / 2.F);
    anim::global_transforms(skeleton, pose, globals);
    expect(near(math::transform_point(globals[1], {}), {0.F, 1.F, 0.F}) && near(math::transform_point(globals[2], {}), {0.F, 2.F, 0.F}),
           "turning the shoulder a quarter turn about Z lifts the elbow to (0, 1, 0) and the hand to (0, 2, 0)");
    anim::skinning_matrices(skeleton, pose, matrices);
    expect(near(skinned(matrices, {2, 0, 0, 0}, {1.F, 0.F, 0.F, 0.F}, tip), {0.F, 2.5F, 0.F}),
           "a vertex on the hand moves with it: " + text(skinned(matrices, {2, 0, 0, 0}, {1.F, 0.F, 0.F, 0.F}, tip)));

    pose[1].rotation = math::from_axis_angle({0.F, 0.F, 1.F}, pi / 2.F);
    anim::skinning_matrices(skeleton, pose, matrices);
    const math::Vec3 forearm{1.5F, 0.F, 0.F};
    expect(near(skinned(matrices, {1, 0, 0, 0}, {1.F, 0.F, 0.F, 0.F}, forearm), {-0.5F, 1.F, 0.F}),
           "bending the elbow too folds the forearm back: "
               + text(skinned(matrices, {1, 0, 0, 0}, {1.F, 0.F, 0.F, 0.F}, forearm)));
    expect(near(skinned(matrices, {0, 1, 0, 0}, {0.5F, 0.5F, 0.F, 0.F}, forearm), {-0.25F, 1.25F, 0.F}),
           "a vertex weighted half to the shoulder and half to the elbow lies between: "
               + text(skinned(matrices, {0, 1, 0, 0}, {0.5F, 0.5F, 0.F, 0.F}, forearm)));
    expect(near(skinned(matrices, {1, 0, 9, 0}, {1.F, 0.F, 0.5F, 0.F}, forearm), {-0.5F, 1.F, 0.F}),
           "a joint index past the skeleton counts as weight 0");

    pose = anim::rest_pose(skeleton);
    pose[0].scale = {2.F, 2.F, 2.F};
    anim::skinning_matrices(skeleton, pose, matrices);
    expect(near(skinned(matrices, {2, 0, 0, 0}, {1.F, 0.F, 0.F, 0.F}, tip), {5.F, 0.F, 0.F}),
           "a parent's scale carries down the chain");
}

anim::Channel channel(std::uint16_t joint, anim::Target target, anim::Interpolation interpolation, std::vector<float> times,
                      std::vector<math::Vec4> values)
{
    return {joint, target, interpolation, std::move(times), std::move(values)};
}

math::Vec3 sampled_translation(const anim::Clip& clip, float seconds, bool loop = false)
{
    anim::Pose pose = anim::rest_pose(arm());
    anim::sample(clip, seconds, loop, pose);
    return pose[0].translation;
}

void test_step_and_linear()
{
    anim::Clip clip;
    clip.channels.push_back(channel(0, anim::Target::Translation, anim::Interpolation::Step, {0.F, 1.F, 2.F},
                                    {v3({0.F, 0.F, 0.F}), v3({1.F, 0.F, 0.F}), v3({3.F, 0.F, 0.F})}));
    std::string error;
    expect(anim::valid_clip(clip, arm(), error), "a step clip is valid: " + error);
    expect(anim::duration(clip) == 2.F, "the clip lasts until its last key");
    expect(near(sampled_translation(clip, 0.5F), {0.F, 0.F, 0.F}), "step holds the first key");
    expect(near(sampled_translation(clip, 1.F), {1.F, 0.F, 0.F}), "step changes at the key");
    expect(near(sampled_translation(clip, 1.99F), {1.F, 0.F, 0.F}), "and holds it until the next");
    expect(near(sampled_translation(clip, 7.F), {3.F, 0.F, 0.F}), "past the end it holds the last key");

    clip.channels[0].interpolation = anim::Interpolation::Linear;
    expect(near(sampled_translation(clip, 0.5F), {0.5F, 0.F, 0.F}), "linear is halfway at half time");
    expect(near(sampled_translation(clip, 1.5F), {2.F, 0.F, 0.F}), "linear runs straight between any two keys");
    expect(near(sampled_translation(clip, 2.5F, true), {0.5F, 0.F, 0.F}), "looping wraps round the duration");
    expect(near(sampled_translation(clip, -0.5F, true), {2.F, 0.F, 0.F}), "and wraps negative times from the end");
    expect(near(sampled_translation(clip, std::nanf("")), {0.F, 0.F, 0.F}), "a non-finite time counts as 0");

    clip.channels[0].times = {0.5F, 1.F, 2.F};
    expect(near(sampled_translation(clip, 0.2F), {0.F, 0.F, 0.F}), "before the first key it holds the first value");

    anim::Clip longer = clip;
    longer.channels.push_back(channel(2, anim::Target::Scale, anim::Interpolation::Linear, {0.F, 3.F},
                                      {v3({1.F, 1.F, 1.F}), v3({2.F, 2.F, 2.F})}));
    expect(anim::duration(longer) == 3.F, "a clip lasts until the last key of its longest channel");
    expect(near(sampled_translation(longer, 4.5F, true), {2.F, 0.F, 0.F}) && near(sampled_translation(longer, 2.5F, true), {3.F, 0.F, 0.F}),
           "and loops round that, holding shorter channels at their last key until it wraps");
    expect(anim::duration(anim::Clip{}) == 0.F, "a clip without channels lasts no time");

    anim::Clip single;
    single.channels.push_back(channel(1, anim::Target::Scale, anim::Interpolation::Linear, {0.F}, {v3({2.F, 2.F, 2.F})}));
    anim::Pose pose = anim::rest_pose(arm());
    anim::sample(single, 3.F, true, pose);
    expect(near(pose[1].scale, {2.F, 2.F, 2.F}), "a single key holds at every time");
    expect(near(pose[0].translation, {}) && near(pose[1].translation, {1.F, 0.F, 0.F}) && near(pose[2].scale, {1.F, 1.F, 1.F}),
           "what the clip does not key stays as it was");
}

void test_rotation()
{
    const math::Vec3 up{0.F, 1.F, 0.F};
    anim::Clip clip;
    clip.channels.push_back(channel(0, anim::Target::Rotation, anim::Interpolation::Linear, {0.F, 1.F},
                                    {q4(math::Quat{}), q4(math::from_axis_angle(up, pi / 2.F))}));
    anim::Pose pose = anim::rest_pose(arm());
    anim::sample(clip, 0.5F, false, pose);
    expect(same_rotation(pose[0].rotation, math::from_axis_angle(up, pi / 4.F)), "halfway between 0 and 90 degrees is 45");
    anim::sample(clip, 0.25F, false, pose);
    expect(same_rotation(pose[0].rotation, math::from_axis_angle(up, pi / 8.F)),
           "a quarter of the way is 22.5 degrees: it turns at a constant speed");

    const math::Quat flipped = math::from_axis_angle(up, pi / 2.F);
    clip.channels[0].values[1] = {-flipped.x, -flipped.y, -flipped.z, -flipped.w};
    anim::sample(clip, 0.5F, false, pose);
    expect(same_rotation(pose[0].rotation, math::from_axis_angle(up, pi / 4.F)),
           "the same rotation keyed with the opposite sign still takes the shorter way");

    clip.channels[0].values[1] = q4(math::from_axis_angle(up, 170.F * pi / 180.F));
    anim::sample(clip, 0.5F, false, pose);
    expect(same_rotation(pose[0].rotation, math::from_axis_angle(up, 85.F * pi / 180.F)), "keys far apart: 0 to 170 is 85");

    clip.channels[0].values[1] = {0.F, 2.F, 0.F, 0.F}; // 180 degrees about Y, not normalised
    anim::sample(clip, 1.F, false, pose);
    expect(std::abs(std::sqrt(pose[0].rotation.x * pose[0].rotation.x + pose[0].rotation.y * pose[0].rotation.y
                              + pose[0].rotation.z * pose[0].rotation.z + pose[0].rotation.w * pose[0].rotation.w)
                    - 1.F)
               < 1e-6F,
           "sampled rotations are normalised");
}

void test_cubic_spline()
{
    anim::Clip clip;
    // Keys at 0 and 2 s from 0 to 1, no tangents: the curve is smoothstep, 3t^2 - 2t^3.
    clip.channels.push_back(channel(0, anim::Target::Translation, anim::Interpolation::CubicSpline, {0.F, 2.F},
                                    {v3({0.F, 0.F, 0.F}), v3({0.F, 0.F, 0.F}), v3({0.F, 0.F, 0.F}),
                                     v3({0.F, 0.F, 0.F}), v3({1.F, 0.F, 0.F}), v3({0.F, 0.F, 0.F})}));
    std::string error;
    expect(anim::valid_clip(clip, arm(), error), "a cubic-spline clip is valid: " + error);
    expect(near(sampled_translation(clip, 1.F), {0.5F, 0.F, 0.F}), "a flat cubic is halfway at half time");
    expect(near(sampled_translation(clip, 0.5F), {0.15625F, 0.F, 0.F}), "and smoothstep a quarter of the way");
    expect(near(sampled_translation(clip, 2.F), {1.F, 0.F, 0.F}), "it ends on the last key's value, not its tangent");

    // An out-tangent of 2 (per second) on the first key, over a 2 s interval:
    // p(0.25) = h10 * 2 s * 2 + h01 * 1 = 0.140625 * 4 + 0.15625 = 0.71875.
    clip.channels[0].values[2] = v3({2.F, 0.F, 0.F});
    expect(near(sampled_translation(clip, 0.5F), {0.71875F, 0.F, 0.F}),
           "tangents follow glTF's Hermite spline, scaled by the key interval: "
               + text(sampled_translation(clip, 0.5F)));
    // An in-tangent on the last key: p(0.75) = h01(0.75) + h10(0.75) * 2 * 2 + h11(0.75) * 2 * 3
    //   = 0.84375 + 0.046875 * 4 + (-0.140625) * 6 = 0.1875.
    clip.channels[0].values[3] = v3({3.F, 0.F, 0.F});
    expect(near(sampled_translation(clip, 1.5F), {0.1875F, 0.F, 0.F}),
           "the next key's in-tangent shapes the end of the interval: " + text(sampled_translation(clip, 1.5F)));
}

void test_blend()
{
    const math::Vec3 up{0.F, 1.F, 0.F};
    anim::Pose a(1);
    anim::Pose b(1);
    b[0].translation = {2.F, 0.F, 0.F};
    b[0].rotation = math::from_axis_angle(up, pi / 2.F);
    b[0].scale = {3.F, 3.F, 3.F};
    anim::Pose out(1);
    anim::blend(a, b, 0.25F, out);
    expect(near(out[0].translation, {0.5F, 0.F, 0.F}) && near(out[0].scale, {1.5F, 1.5F, 1.5F}),
           "blending moves translation and scale in a straight line");
    anim::blend(a, b, 0.5F, out);
    expect(same_rotation(out[0].rotation, math::from_axis_angle(up, pi / 4.F)), "and rotation halfway round");
    anim::blend(a, b, 2.F, out);
    expect(near(out[0].translation, {2.F, 0.F, 0.F}), "the weight is clamped to 1");
    anim::blend(a, b, -1.F, out);
    expect(near(out[0].translation, {}), "and to 0");
    b[0].rotation = {-b[0].rotation.x, -b[0].rotation.y, -b[0].rotation.z, -b[0].rotation.w};
    anim::blend(a, b, 0.5F, a);
    expect(same_rotation(a[0].rotation, math::from_axis_angle(up, pi / 4.F)) && near(a[0].translation, {1.F, 0.F, 0.F}),
           "it takes the shorter way, and may write over its input");
}

void test_refusals()
{
    std::string error;
    const auto refused_skeleton = [&error](anim::Skeleton skeleton, std::string_view what) {
        expect(!anim::valid_skeleton(skeleton, error) && !error.empty(), what);
    };
    refused_skeleton({}, "an empty skeleton is refused");
    anim::Skeleton big;
    big.joints.resize(anim::max_joints + 1);
    refused_skeleton(big, "too many joints are refused");
    anim::Skeleton skeleton = arm();
    skeleton.joints[1].parent = 2;
    refused_skeleton(skeleton, "a parent after its child is refused");
    skeleton = arm();
    skeleton.joints[1].parent = 1;
    refused_skeleton(skeleton, "a joint that is its own parent is refused");
    skeleton = arm();
    skeleton.joints[2].rest.translation.x = std::nanf("");
    refused_skeleton(skeleton, "a non-finite rest pose is refused");
    skeleton = arm();
    skeleton.joints[2].inverse_bind.m[3][0] = INFINITY;
    refused_skeleton(skeleton, "a non-finite inverse bind matrix is refused");
    skeleton = arm();
    skeleton.joints[0].rest.rotation = {0.F, 0.F, 0.F, 2.F};
    refused_skeleton(skeleton, "a rotation that is not of unit length is refused");
    skeleton = arm();
    skeleton.joints[0].rest.scale = {1.F, 0.F, 1.F};
    refused_skeleton(skeleton, "a zero scale is refused");

    const auto refused_clip = [&error](const anim::Channel& c, std::string_view what) {
        anim::Clip clip;
        clip.channels.push_back(c);
        expect(!anim::valid_clip(clip, arm(), error) && !error.empty(), what);
    };
    using anim::Interpolation;
    using anim::Target;
    refused_clip(channel(3, Target::Translation, Interpolation::Linear, {0.F}, {{}}), "a joint outside the skeleton is refused");
    refused_clip(channel(0, Target::Translation, Interpolation::Linear, {}, {}), "a channel without keys is refused");
    refused_clip(channel(0, Target::Translation, Interpolation::Linear, {1.F, 1.F}, {{}, {}}), "repeated key times are refused");
    refused_clip(channel(0, Target::Translation, Interpolation::Linear, {1.F, 0.5F}, {{}, {}}), "times going backwards are refused");
    refused_clip(channel(0, Target::Translation, Interpolation::Linear, {-1.F}, {{}}), "a negative time is refused");
    refused_clip(channel(0, Target::Translation, Interpolation::Linear, {INFINITY}, {{}}), "an infinite time is refused");
    refused_clip(channel(0, Target::Translation, Interpolation::Linear, {0.F, 1.F}, {{}}), "too few values are refused");
    refused_clip(channel(0, Target::Translation, Interpolation::CubicSpline, {0.F, 1.F}, {{}, {}}),
                 "a cubic spline without its tangents is refused");
    refused_clip(channel(0, Target::Rotation, Interpolation::Step, {0.F}, {{std::nanf(""), 0.F, 0.F, 1.F}}),
                 "a non-finite value is refused");
    anim::Clip twice;
    twice.channels.push_back(channel(1, Target::Rotation, Interpolation::Linear, {0.F}, {{0.F, 0.F, 0.F, 1.F}}));
    twice.channels.push_back(channel(1, Target::Rotation, Interpolation::Step, {0.F}, {{0.F, 0.F, 0.F, 1.F}}));
    expect(!anim::valid_clip(twice, arm(), error) && error.find("twice") != std::string::npos,
           "a joint's rotation keyed by two channels is refused");
    twice.channels[1].target = Target::Translation;
    expect(anim::valid_clip(twice, arm(), error), "its rotation and translation in two channels are fine: " + error);
}

}

int main()
{
    test_skinning();
    test_step_and_linear();
    test_rotation();
    test_cubic_spline();
    test_blend();
    test_refusals();
    if (g_failures != 0) {
        std::cerr << g_failures << " animation assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "animation passed\n";
    return EXIT_SUCCESS;
}
