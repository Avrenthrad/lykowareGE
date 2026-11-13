// physics.ragdoll_blend (ADR 0080): blending animation and physics on a ragdoll. Weight 0 is the
// animated pose, 1 is the physics pose. The sample path (frames through FixedStep) and a headless
// replay of the recorded weights agree at 30, 60 and 144 Hz; weights outside 0..1 are refused.

#include "ragdoll_blend.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {
namespace physics = lykoware::physics;
namespace animation = lykoware::animation;
namespace math = lykoware::math;

int failures = 0;
void check(bool ok, const std::string& why)
{
    if (!ok) {
        std::cerr << "FAIL: " << why << '\n';
        ++failures;
    }
}

constexpr double seconds = 4.0;
// At weight 0 the bodies follow the walk: no joint of the read pose is further than this from the
// animated pose (the joints' solver leaves a little; locally about 0.009 rad).
constexpr float follow_turn = .02F; // radians

float angle_between(math::Quat a, math::Quat b)
{
    const math::Quat d = math::conjugate(a) * b;
    return 2.F * std::asin(std::min(1.F, math::length(math::Vec3{d.x, d.y, d.z})));
}

float worst_turn(const animation::Pose& a, const animation::Pose& b)
{
    float worst = 0.F;
    for (std::size_t j = 0; j < a.size() && j < b.size(); ++j) {
        worst = std::max(worst, angle_between(a[j].rotation, b[j].rotation));
    }
    return worst;
}

void test_paths_agree()
{
    std::vector<ragdoll_blend::Run> runs;
    for (const std::uint32_t hz : {30U, 60U, 144U}) {
        std::string error;
        const auto run = ragdoll_blend::run_sample(hz, seconds, error);
        check(run.has_value(), std::to_string(hz) + " Hz: the sample runs: " + error);
        if (!run) {
            continue;
        }
        const auto replayed = ragdoll_blend::replay(run->weights, error);
        std::cout << hz << " Hz: " << run->weights.size() << " ticks, sample " << std::hex << run->hash << ", replay "
                  << (replayed ? *replayed : 0) << std::dec << '\n';
        check(replayed && *replayed == run->hash, std::to_string(hz) + " Hz: the replay matches the sample bit for bit");
        runs.push_back(*run);
    }
    // Every rate ran the same ticks with the same weights: replaying the ticks all of them reached
    // gives one hash.
    std::size_t common = std::numeric_limits<std::size_t>::max();
    for (const auto& run : runs) {
        common = std::min(common, run.weights.size());
    }
    check(common >= 230, "every rate ran close to four seconds of ticks");
    std::vector<std::uint64_t> hashes;
    for (const auto& run : runs) {
        std::string error;
        const std::vector<float> prefix(run.weights.begin(), run.weights.begin() + static_cast<std::ptrdiff_t>(common));
        const auto hash = ragdoll_blend::replay(prefix, error);
        hashes.push_back(hash ? *hash : 0);
    }
    check(hashes.size() == 3 && hashes[0] == hashes[1] && hashes[1] == hashes[2], "30, 60 and 144 Hz agree tick for tick");

    // Deliberate break: one recorded weight off by 0.01 gives a different world, so the check
    // above can fail.
    if (!runs.empty()) {
        auto tampered = runs.front().weights;
        tampered[70] += .01F; // weight about 0.33 there
        std::string error;
        const auto hash = ragdoll_blend::replay(tampered, error);
        check(hash && *hash != runs.front().hash, "a changed weight changes the replay");
    }
}

void test_weights()
{
    std::string error;
    auto scene = ragdoll_blend::Scene::make(error);
    check(scene.has_value(), "the scene is made: " + error);
    if (!scene) {
        return;
    }
    // One second at weight 0: the bodies follow the walk, and the drawn pose is the animated one.
    for (int i = 0; i < 60; ++i) {
        scene->tick(0.F);
    }
    const animation::Pose animated = scene->animated();
    check(worst_turn(scene->drawn(), animated) < 1e-6F, "at weight 0 the drawn pose is the animated pose");
    animation::Pose physical;
    scene->doll().read_pose(scene->world(), animated, physical);
    const float following = worst_turn(animated, physical);
    std::cout << "weight 0: bodies within " << following << " rad of the walk\n";
    check(following < follow_turn, "at weight 0 the bodies follow the animated pose");
    const double standing = scene->world().state(scene->doll().bodies()[1])->position.y;

    // A second and a half at weight 1: nothing holds it up, so it falls.
    for (int i = 0; i < 90; ++i) {
        scene->tick(1.F);
    }
    const double fallen = scene->world().state(scene->doll().bodies()[1])->position.y;
    std::cout << "torso height: " << standing << " m following, " << fallen << " m as a ragdoll\n";
    check(fallen < standing - .3, "at weight 1 the ragdoll falls");

    // Between: the drawn pose is the blend of the two.
    scene->tick(.3F);
    const auto drawn = scene->drawn();
    animation::Pose expected;
    animation::Pose bodies;
    const animation::Pose walk = scene->animated();
    scene->doll().read_pose(scene->world(), walk, bodies);
    animation::blend(walk, bodies, .3F, expected);
    check(worst_turn(drawn, expected) < 1e-6F, "between 0 and 1 the drawn pose is the blend of animated and physics");

    // Refusals change nothing.
    const auto before = scene->hash();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    check(!scene->tick(-.1F) && !scene->tick(1.1F) && !scene->tick(nan), "weights outside 0..1 are refused");
    check(scene->hash() == before, "a refused weight leaves the world alone");
    animation::Pose out;
    check(!scene->doll().blended_pose(scene->world(), walk, 2.F, out) && out.empty(), "blended_pose refuses a weight above 1");
    check(!ragdoll_blend::replay({0.F, .5F, 1.5F}, error) && !error.empty(), "a replay with a weight above 1 is refused");
}

} // namespace

int main()
{
    test_paths_agree();
    test_weights();
    if (failures == 0) {
        std::cout << "physics.ragdoll_blend: all checks passed\n";
    }
    return failures == 0 ? 0 : 1;
}
