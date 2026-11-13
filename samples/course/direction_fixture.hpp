#pragma once
// The direction-change fixture (ADR 0074): a fixed sequence of pushes that exercises
// directional fluidity. Shared by the player.directions test and the sandbox's --directions,
// so what a person watches is what the test measures.
//
// The fixture is keyed to simulation ticks at 60 a second, so it plays the same at any frame
// rate. Push directions are relative to the camera, as a player's stick is; the camera stays
// still except in the "camera sweep" segment.

#include <lykoware/player.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace direction_fixture {

inline constexpr int ticks_per_second = 60;

struct Segment {
    const char* name;
    int ticks;
    // The push's direction relative to the camera at the segment's start, in degrees: 0 forward,
    // 90 left, -90 right, 180 back. It turns by `push_turn` degrees a second through the segment.
    float push_degrees;
    float push_turn;
    float push;   // stick deflection, 0..1
    bool run;     // sprint held
    bool aim;     // face where the camera looks, and strafe
    float camera; // degrees a second the camera turns (positive to the left)
};

// Cuts are measured from the way the character is already running: 45, 90, 135 and 180 degrees.
inline constexpr std::array<Segment, 16> segments{{
    {"settle", 30, 0.F, 0.F, 0.F, false, false, 0.F},
    {"run", 90, 0.F, 0.F, 1.F, false, false, 0.F},
    {"cut 45", 60, 45.F, 0.F, 1.F, false, false, 0.F},
    {"cut 90", 60, -45.F, 0.F, 1.F, false, false, 0.F},
    {"cut 135", 60, 90.F, 0.F, 1.F, false, false, 0.F},
    {"cut 180", 60, -90.F, 0.F, 1.F, false, false, 0.F},
    {"stop", 45, 0.F, 0.F, 0.F, false, false, 0.F},
    {"loop left", 180, 0.F, 120.F, 1.F, false, false, 0.F},
    {"loop right", 180, 0.F, -120.F, 1.F, false, false, 0.F},
    {"camera sweep", 120, 0.F, 0.F, 1.F, false, false, 60.F},
    {"strafe forward", 45, 0.F, 0.F, 1.F, false, true, 0.F},
    {"strafe right", 45, -90.F, 0.F, 1.F, false, true, 0.F},
    {"strafe back", 45, 180.F, 0.F, 1.F, false, true, 0.F},
    {"strafe left", 45, 90.F, 0.F, 1.F, false, true, 0.F},
    {"backpedal diagonal", 60, 135.F, 0.F, 1.F, false, true, 0.F},
    {"release", 60, 0.F, 0.F, 0.F, false, false, 0.F},
}};

inline constexpr int total_ticks()
{
    int total = 0;
    for (const auto& segment : segments) {
        total += segment.ticks;
    }
    return total;
}

// The segment playing at `tick`, and how many ticks into it; past the end, the last segment.
struct Position {
    std::size_t segment = 0;
    int into = 0;
};
inline Position position(int tick)
{
    int start = 0;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        if (tick < start + segments[i].ticks) {
            return {i, tick - start};
        }
        start += segments[i].ticks;
    }
    return {segments.size() - 1, segments.back().ticks};
}

// The push's direction relative to the camera at `tick`, in degrees.
inline float push_degrees(int tick)
{
    const Position at = position(tick);
    const Segment& segment = segments[at.segment];
    return segment.push_degrees + segment.push_turn * static_cast<float>(at.into) / static_cast<float>(ticks_per_second);
}

// The intent at simulation tick `tick`, for a frame of `frame_seconds` (which scales only the
// camera's turn, as a held stick's would).
inline lykoware::PlayerIntent intent(int tick, float frame_seconds)
{
    const Position at = position(tick);
    const Segment& segment = segments[at.segment];
    constexpr float radians = std::numbers::pi_v<float> / 180.F;
    const float angle = push_degrees(tick) * radians;
    lykoware::PlayerIntent out;
    out.forward = std::cos(angle) * segment.push;
    out.right = -std::sin(angle) * segment.push;
    out.run = segment.run;
    out.aim = segment.aim;
    out.yaw = std::isfinite(frame_seconds) && frame_seconds > 0.F ? segment.camera * radians * frame_seconds : 0.F;
    return out;
}

}
