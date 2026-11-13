// animation.stride (ADR 0074, "Animation"): the mannequin's gait follows the locomotion motor and the
// speed physics really achieved, so a planted foot does not slide over the ground.
//
// Foot world position = body travel + the foot's model-space position under the drawn pose. The body
// faces -Z, so it travels along -Z.

#include "animated_mannequin.hpp"

#include <lykoware/locomotion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

namespace animation = lykoware::animation;
namespace math = lykoware::math;
namespace loco = lykoware::locomotion;

int failures = 0;
void expect(bool ok, const std::string& why)
{
    if (!ok) {
        std::cerr << "FAIL: " << why << '\n';
        ++failures;
    }
}

constexpr float dt = 1.F / 60.F;
constexpr float stance_height = 0.008F;

loco::State moving(float speed)
{
    loco::State state;
    state.speed = speed;
    state.velocity = {0.F, 0.F, -speed};
    state.gait = speed > 5.5F ? loco::Gait::Sprint : speed > 4.F ? loco::Gait::Run : speed > 2.4F ? loco::Gait::Jog
        : speed > 0.1F ? loco::Gait::Walk : loco::Gait::Idle;
    state.legs = speed > 0.1F ? loco::Legs::Moving : loco::Legs::Stopped;
    return state;
}

enum class Driver { Motor, Old };

// Runs `seconds` of travel at a constant speed and returns the worst world speed of a foot that is on
// the ground in two consecutive frames, after one second of settling, and how many such samples.
struct Slide {
    float worst = 0.F;
    int samples = 0;
};
Slide measure_slide(const training_mannequin::Asset& asset, float speed, Driver driver, float seconds = 5.F)
{
    training_mannequin::Playback playback;
    const auto motor = moving(speed);
    float body = 0.F;
    std::array<math::Vec3, 2> before{};
    bool have_before = false;
    Slide slide;
    const int steps = static_cast<int>(seconds / dt);
    for (int step = 0; step < steps; ++step) {
        if (driver == Driver::Motor) {
            playback.advance_motor(asset, dt, motor, speed, true, 0.F);
        } else {
            playback.advance(asset, dt, speed);
        }
        body -= speed * dt;
        std::array<math::Vec3, 2> now{};
        for (std::size_t leg = 0; leg < 2; ++leg) {
            now[leg] = training_mannequin::foot_in_model(asset, playback.blended, static_cast<std::uint16_t>(5 + leg));
            now[leg].z += body;
        }
        if (have_before && static_cast<float>(step) * dt > 1.F) {
            for (std::size_t leg = 0; leg < 2; ++leg) {
                if (now[leg].y < stance_height && before[leg].y < stance_height) {
                    const float world_speed = std::abs(now[leg].z - before[leg].z) / dt;
                    slide.worst = std::max(slide.worst, world_speed);
                    ++slide.samples;
                }
            }
        }
        before = now;
        have_before = true;
    }
    return slide;
}

float angle_between(const math::Quat& a, const math::Quat& b)
{
    const float dot = std::abs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
    return 2.F * std::acos(std::min(1.F, dot)) * 180.F / 3.14159265F;
}

} // namespace

int main(int argc, char** argv)
{
    const auto asset = training_mannequin::make();

    // 1. Authored speeds are measured, positive, and match the clips' own stride and cycle time.
    expect(asset.walk_speed > 0.F && asset.run_speed > 0.F && asset.walk_speed < asset.run_speed,
           "measured authored speeds are positive and walk < run");
    expect(std::abs(asset.walk_speed - .88F / .5F) < .02F * (.88F / .5F),
           "walk authored speed is its stride over half its cycle (1.76 m/s)");
    expect(std::abs(asset.run_speed - 1.25F / .25F) < .02F * (1.25F / .25F),
           "run authored speed is its stride over half its cycle (5.0 m/s)");
    std::cout << "authored speeds: walk " << asset.walk_speed << " m/s, run " << asset.run_speed << " m/s\n";

    // 2. No foot slide at the motor's gait speeds; the old speed/2 guess is the positive control.
    for (const float speed : {1.6F, 3.2F, 5.F, 7.F}) {
        const auto matched = measure_slide(asset, speed, Driver::Motor);
        const auto guessed = measure_slide(asset, speed, Driver::Old);
        std::cout << "speed " << speed << " m/s: stance foot worst " << matched.worst << " m/s over " << matched.samples
                  << " samples; old speed/2 guess " << guessed.worst << " m/s over " << guessed.samples << '\n';
        expect(matched.samples > 100, "stance samples exist at " + std::to_string(speed) + " m/s");
        expect(matched.worst < .15F, "the stance foot does not slide at " + std::to_string(speed) + " m/s");
        expect(guessed.worst > .15F && guessed.worst > 2.F * matched.worst,
               "the old speed/2 guess slides more at " + std::to_string(speed) + " m/s");
    }

    // 3. Stride follows the speed physics achieved, not the speed the motor wanted: the motor says 7 but
    // physics gave 3.2 (a wall, a slope); the feet must match 3.2.
    {
        training_mannequin::Playback playback;
        const auto wanted = moving(7.F);
        float body = 0.F;
        math::Vec3 before{};
        float worst = 0.F;
        for (int step = 0; step < 300; ++step) {
            playback.advance_motor(asset, dt, wanted, 3.2F, true, 0.F);
            body -= 3.2F * dt;
            auto foot = training_mannequin::foot_in_model(asset, playback.blended, 6);
            foot.z += body;
            if (step > 90 && foot.y < stance_height && before.y < stance_height) {
                worst = std::max(worst, std::abs(foot.z - before.z) / dt);
            }
            before = foot;
        }
        std::cout << "wanted 7 but actual 3.2: worst " << worst << " m/s\n";
        expect(worst < .15F, "stride follows the actual speed, not the wanted speed");
    }

    // 4. No pops. (a) With the stride phase held (pivoting), the pose can change only through the
    // blend weights: sweeping the speed 0 -> 7 -> 0, with the motor's gait changing as it goes, no
    // joint turns more than 1.5 degrees in a step. (b) Free running over the same ramp, no joint turns
    // more than the gait itself does at a steady sprint, plus a little.
    {
        const auto ramp = [&](bool hold, float& worst) {
            training_mannequin::Playback playback;
            std::vector<math::Quat> previous;
            const int steps = 60 * 8;
            for (int step = 0; step < steps; ++step) {
                const float t = static_cast<float>(step) * dt;
                const float speed = t < 4.F ? 7.F * t / 4.F : 7.F * (8.F - t) / 4.F;
                auto motor = moving(speed);
                if (hold) { motor.legs = loco::Legs::Pivoting; }
                playback.advance_motor(asset, dt, motor, speed, true, 0.F);
                if (!previous.empty()) {
                    for (std::size_t joint = 0; joint < previous.size(); ++joint) {
                        worst = std::max(worst, angle_between(previous[joint], playback.blended[joint].rotation));
                    }
                }
                previous.clear();
                for (const auto& joint : playback.blended) { previous.push_back(joint.rotation); }
            }
        };
        float held = 0.F;
        float free_ramp = 0.F;
        ramp(true, held);
        ramp(false, free_ramp);
        float steady = 0.F;
        {
            training_mannequin::Playback playback;
            std::vector<math::Quat> previous;
            for (int step = 0; step < 600; ++step) {
                playback.advance_motor(asset, dt, moving(7.F), 7.F, true, 0.F);
                if (step > 60) {
                    for (std::size_t joint = 0; joint < previous.size(); ++joint) {
                        steady = std::max(steady, angle_between(previous[joint], playback.blended[joint].rotation));
                    }
                }
                previous.clear();
                for (const auto& joint : playback.blended) { previous.push_back(joint.rotation); }
            }
        }
        std::cout << "speed ramp: worst per-step joint turn " << held << " deg with the phase held, " << free_ramp
                  << " deg running free (steady sprint " << steady << " deg)\n";
        expect(held < 1.5F, "with the phase held, no joint turns more than 1.5 degrees in a step as the gait changes");
        expect(free_ramp < steady + 2.F, "no joint turns more over a speed ramp than at a steady sprint, plus 2 degrees");
    }

    // 5. A pivot holds the stride phase; it resumes afterwards.
    {
        training_mannequin::Playback playback;
        auto motor = moving(3.F);
        for (int step = 0; step < 120; ++step) { playback.advance_motor(asset, dt, motor, 3.F, true, 0.F); }
        const float before = playback.stride_phase;
        const auto pose = playback.blended;
        motor.legs = loco::Legs::Pivoting;
        for (int step = 0; step < 30; ++step) { playback.advance_motor(asset, dt, motor, 3.F * (1.F - static_cast<float>(step) / 30.F), true, 0.F); }
        expect(playback.stride_phase == before, "the stride phase does not advance while pivoting");
        expect(angle_between(pose[5].rotation, playback.blended[5].rotation) < 20.F, "a pivot stays near the planted pose");
        motor.legs = loco::Legs::Moving;
        for (int step = 0; step < 30; ++step) { playback.advance_motor(asset, dt, motor, 3.F, true, 0.F); }
        expect(playback.stride_phase != before, "the stride phase resumes after the pivot");
    }

    // 6. Lean appears in the pose with the right sign and stays bounded.
    {
        const auto torso_up = [&](float forward, float side) {
            training_mannequin::Playback playback;
            auto motor = moving(3.F);
            motor.lean_forward = forward;
            motor.lean_side = side;
            for (int step = 0; step < 10; ++step) { playback.advance_motor(asset, dt, motor, 3.F, true, 0.F); }
            return math::rotate(playback.blended[1].rotation, {0.F, 1.F, 0.F});
        };
        const auto level = torso_up(0.F, 0.F);
        const auto ahead = torso_up(.2F, 0.F);
        const auto right = torso_up(0.F, .1F);
        expect(std::abs(level.x) < 1e-5F && std::abs(level.z) < 1e-5F, "no lean, upright torso");
        expect(ahead.z < -.15F && std::abs(ahead.x) < 1e-4F, "forward lean tips the torso toward -Z (the way it faces)");
        expect(right.x > .05F && std::abs(right.z) < 1e-4F, "side lean to the right tips the torso toward +X");
        const auto back = torso_up(-.2F, -.1F);
        expect(back.z > .1F && back.x < -.05F, "negative lean tips the other way");
        const auto extreme = torso_up(3.F, -3.F);
        expect(std::abs(std::asin(std::clamp(extreme.z, -1.F, 1.F))) <= asset.lean_limit + 1e-3F
                   && std::abs(std::asin(std::clamp(extreme.x, -1.F, 1.F))) <= asset.lean_limit + 1e-3F,
               "lean stays within the asset's limit whatever the motor says");
        expect(asset.lean_limit <= .4F, "the lean limit is small");
    }

    // 7. Air and landing still work: jump, fall, land, then back to the ground gait.
    {
        training_mannequin::Playback playback;
        auto motor = moving(3.F);
        for (int step = 0; step < 30; ++step) { playback.advance_motor(asset, dt, motor, 3.F, true, 0.F); }
        playback.advance_motor(asset, dt, motor, 3.F, false, 4.F);
        expect(playback.selector.state() == locomotion::State::Jump, "leaving the ground with upward speed jumps");
        playback.advance_motor(asset, dt, motor, 3.F, false, -4.F);
        expect(playback.selector.state() == locomotion::State::Fall, "falling");
        playback.advance_motor(asset, dt, motor, 3.F, true, 0.F);
        expect(playback.selector.state() == locomotion::State::Land, "touching down lands");
        for (int step = 0; step < 60; ++step) { playback.advance_motor(asset, dt, motor, 3.F, true, 0.F); }
        expect(playback.selector.state() == locomotion::State::Walk, "landing releases into the gait");
    }

    // 8. The sandbox's PlayerBody drives the animation from the motor when the player has one. (The
    // sandbox has no option that gives its player a profile yet, so check the source.)
    if (argc > 1) {
        std::ifstream in(argv[1]);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const auto place = text.find("void place(const lykoware::PlayerCharacter&");
        const auto report = text.find("void report() const", place);
        const auto body = place == std::string::npos || report == std::string::npos ? std::string{} : text.substr(place, report - place);
        const auto locomotion_check = body.find("player.locomotion()");
        const auto motor_call = body.find("playback_.advance_motor(");
        const auto old_call = body.find("playback_.advance_character(");
        expect(!body.empty(), "found PlayerBody::place in the sandbox source");
        expect(locomotion_check != std::string::npos && motor_call != std::string::npos && locomotion_check < motor_call,
               "PlayerBody::place calls advance_motor when the player has a motor");
        expect(old_call != std::string::npos && motor_call < old_call,
               "PlayerBody::place keeps advance_character for players without a motor");
    } else {
        expect(false, "the sandbox source path is passed");
    }

    return failures == 0 ? 0 : 1;
}
