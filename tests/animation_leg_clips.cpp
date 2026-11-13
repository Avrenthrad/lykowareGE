// animation.leg_clips (ADR 0074, "Starts, stops and pivots"): the motor's leg state plays the
// asset's start, stop and pivot clips where it has them, and keeps the gait blend where it does not.
// animation.stride already checks authored speeds, stance slide, stride following actual speed, the
// gait ramp, the pivot phase hold, lean and air; this test does not repeat those.

#include "animated_mannequin.hpp"

#include <lykoware/locomotion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

namespace math = lykoware::math;
namespace loco = lykoware::locomotion;
using training_mannequin::LegClip;

int failures = 0;
void expect(bool ok, const std::string& why)
{
    if (!ok) {
        std::cerr << "FAIL: " << why << '\n';
        ++failures;
    }
}

constexpr float dt = 1.F / 60.F;
constexpr float pop_bound = 6.F;     // degrees one joint may turn in one step (animation.stride's)
constexpr float support_height = .02F; // the lower foot is at most this far off the ground

float angle_between(const math::Quat& a, const math::Quat& b)
{
    const float dot = std::abs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
    return 2.F * std::acos(std::min(1.F, dot)) * 180.F / 3.14159265F;
}

float worst_turn(const lykoware::animation::Pose& a, const lykoware::animation::Pose& b)
{
    float worst = 0.F;
    for (std::size_t joint = 0; joint < a.size() && joint < b.size(); ++joint) {
        worst = std::max(worst, angle_between(a[joint].rotation, b[joint].rotation));
    }
    return worst;
}

struct Step {
    loco::Legs legs;
    float speed;
};

// Stand, start, run, pivot, carry on, brake, stand.
std::vector<Step> script()
{
    std::vector<Step> steps;
    for (int i = 0; i < 30; ++i) { steps.push_back({loco::Legs::Stopped, 0.F}); }
    for (int i = 0; i < 40; ++i) { steps.push_back({loco::Legs::Moving, 3.F * static_cast<float>(i + 1) / 40.F}); }
    for (int i = 0; i < 60; ++i) { steps.push_back({loco::Legs::Moving, 3.F}); }
    for (int i = 0; i < 20; ++i) { steps.push_back({loco::Legs::Pivoting, 3.F - 2.F * static_cast<float>(i + 1) / 20.F}); }
    for (int i = 0; i < 30; ++i) { steps.push_back({loco::Legs::Moving, 1.F + 2.F * static_cast<float>(i + 1) / 30.F}); }
    for (int i = 0; i < 40; ++i) { steps.push_back({loco::Legs::Braking, 3.F * (1.F - static_cast<float>(i + 1) / 40.F)}); }
    for (int i = 0; i < 60; ++i) { steps.push_back({loco::Legs::Stopped, 0.F}); }
    return steps;
}

loco::State motor_at(const Step& step)
{
    loco::State state;
    state.speed = step.speed;
    state.velocity = {0.F, 0.F, -step.speed};
    state.gait = step.speed > 2.4F ? loco::Gait::Jog : step.speed > .1F ? loco::Gait::Walk : loco::Gait::Idle;
    state.legs = step.legs;
    return state;
}

struct Run {
    std::vector<LegClip> selected;
    std::vector<float> top_weight;
    float worst_pop = 0.F;
    float worst_support = 0.F; // the lower foot's worst height while on the ground
    float settled_feet = 0.F;  // the higher foot's height at the end, standing
};

Run play(const training_mannequin::Asset& asset)
{
    training_mannequin::Playback playback;
    Run run;
    lykoware::animation::Pose previous;
    for (const auto& step : script()) {
        playback.advance_motor(asset, dt, motor_at(step), step.speed, true, 0.F);
        if (!previous.empty()) { run.worst_pop = std::max(run.worst_pop, worst_turn(previous, playback.blended)); }
        previous = playback.blended;
        run.selected.push_back(playback.leg_clip);
        run.top_weight.push_back(*std::max_element(playback.leg_clip_weight.begin(), playback.leg_clip_weight.end()));
        float lower = 1e9F;
        float higher = -1e9F;
        for (std::uint16_t leg : {std::uint16_t{5}, std::uint16_t{6}}) {
            const float y = training_mannequin::foot_in_model(asset, playback.blended, leg).y;
            lower = std::min(lower, y);
            higher = std::max(higher, y);
        }
        run.worst_support = std::max(run.worst_support, lower);
        run.settled_feet = higher;
    }
    return run;
}

bool selects(const Run& run, std::size_t from, std::size_t to, LegClip clip)
{
    for (std::size_t i = from; i < to && i < run.selected.size(); ++i) {
        if (run.selected[i] == clip) { return true; }
    }
    return false;
}

} // namespace

int main()
{
    const auto plain = training_mannequin::make();
    auto clipped = plain;
    training_mannequin::add_placeholder_leg_clips(clipped);
    auto no_pivot = clipped;
    no_pivot.pivot = {};

    // Phase boundaries in script(): start 30..70, pivot 130..150, brake 180..220, stand 220..280.
    const auto with = play(clipped);
    const auto without = play(plain);
    const auto partial = play(no_pivot);

    // 1. Each clip is selected by its leg state, and reaches full weight.
    expect(selects(with, 30, 70, LegClip::Start), "Stopped -> Moving selects the start clip");
    expect(selects(with, 130, 150, LegClip::Pivot), "Pivoting selects the pivot clip");
    expect(selects(with, 180, 220, LegClip::Stop), "Braking selects the stop clip");
    expect(!selects(with, 70, 130, LegClip::Stop) && !selects(with, 70, 130, LegClip::Pivot),
           "steady running selects no stop or pivot clip");
    expect(with.top_weight[150 - 1] > .9F, "the pivot clip reaches full weight");
    expect(with.top_weight[220 - 1] > .9F, "the stop clip reaches full weight");
    expect(with.top_weight.back() < .01F, "the stop clip fades out once finished and standing");

    // 2. A start needs an observed stop: a character already moving never plays it, so the stride
    //    numbers animation.stride holds do not change.
    {
        training_mannequin::Playback playback;
        bool started = false;
        for (int i = 0; i < 60; ++i) {
            playback.advance_motor(clipped, dt, motor_at({loco::Legs::Moving, 3.F}), 3.F, true, 0.F);
            started = started || playback.leg_clip == LegClip::Start;
        }
        expect(!started, "moving from the first step plays no start clip");
    }

    // 3. Missing clips fall back to the gait blend: nothing selected, no weight.
    expect(!selects(without, 0, without.selected.size(), LegClip::Start)
               && !selects(without, 0, without.selected.size(), LegClip::Stop)
               && !selects(without, 0, without.selected.size(), LegClip::Pivot),
           "an asset without clips selects none");
    expect(*std::max_element(without.top_weight.begin(), without.top_weight.end()) == 0.F,
           "an asset without clips gives them no weight");
    expect(!selects(partial, 130, 150, LegClip::Pivot) && selects(partial, 180, 220, LegClip::Stop),
           "a missing pivot clip falls back while the stop clip still plays");

    // 4. No pop, entering or leaving any clip, or falling back.
    std::cout << "worst per-step joint turn: clips " << with.worst_pop << " deg, none " << without.worst_pop
              << " deg, no pivot " << partial.worst_pop << " deg\n";
    expect(with.worst_pop < pop_bound, "clips fade in and out without a pop");
    expect(without.worst_pop < pop_bound, "the fallback blend does not pop");
    expect(partial.worst_pop < pop_bound, "falling back for one state does not pop");

    // 5. Deliberate break: snapping the stop clip in at full weight, mid-clip, from a running pose
    //    would exceed the bound, so check 4 can fail.
    {
        training_mannequin::Playback playback;
        for (int i = 0; i < 90; ++i) {
            playback.advance_motor(clipped, dt, motor_at({loco::Legs::Moving, 3.F}), 3.F, true, 0.F);
        }
        auto snapped = playback.blended;
        lykoware::animation::sample(clipped.stop, .25F, false, snapped);
        const float jump = worst_turn(playback.blended, snapped);
        std::cout << "snapping the stop clip in one step: " << jump << " deg\n";
        expect(jump > pop_bound, "a snapped clip would be caught as a pop");
    }

    // 6. Feet still plant: the lower foot stays on the ground throughout, and both are down at the end.
    std::cout << "lower foot worst height: clips " << with.worst_support << " m, none " << without.worst_support
              << " m; standing feet " << with.settled_feet << " m\n";
    expect(with.worst_support < support_height, "with clips, one foot is always on the ground");
    expect(with.settled_feet < support_height, "after the stop clip, both feet are on the ground");

    return failures == 0 ? 0 : 1;
}
