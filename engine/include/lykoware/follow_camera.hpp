#pragma once

#include <lykoware/math.hpp>
#include <lykoware/physics.hpp>
#include <lykoware/render.hpp>

namespace lykoware {

struct FollowCameraConfig {
    // Distance from the pivot the camera keeps when nothing is in the way, in metres. 0 is
    // first person: the eye is at the pivot.
    float distance = 4.F;
    // Closest the camera comes to the pivot when something is in the way, in metres.
    float min_distance = 0.3F;
    // Space kept between the camera and anything it would pass through, in metres; at least
    // `near`, so the near plane never cuts into a wall.
    float clearance = 0.2F;
    // Pitch limits in radians; positive pitch looks down on the pivot from above.
    float min_pitch = -0.6F;
    float max_pitch = 1.3F;
    // How fast the camera moves back out once the way is clear, in metres per second. Pulling
    // in is immediate.
    float return_speed = 6.F;
    float vertical_fov = 1.0471976F; // 60 degrees
    float near = 0.1F;
};

// A third-person orbit camera that never looks through or from inside walls (ADR 0044).
//
// The camera sits `distance` behind and above a pivot (a character's head, say), at a yaw and
// pitch the player turns. Each update it probes the way from the pivot to where it would be with
// five rays against the physics world, fanning out from the pivot to the camera's place and to
// four points `clearance` across the view from it. It stops `clearance` short of the nearest
// hit. The pivot itself must be in open space. It pulls in at once and eases
// back out at `return_speed`. Characters are not physics bodies, so the player's own character
// never blocks the view.
//
// Presentation only: it reads the physics world and changes nothing in it, so the simulation
// and its replays do not depend on the camera.
//
// Ownership: a value type. Threading: one thread at a time, with no physics step running.
class FollowCamera {
public:
    explicit FollowCamera(const FollowCameraConfig& config = {});

    // Turns the view by these angles, in radians. Pitch is clamped to the configured limits;
    // yaw wraps to [-pi, pi). Non-finite input is ignored.
    void turn(float yaw_delta, float pitch_delta);
    // Sets the unobstructed distance, clamped to at least 0 (first person). Non-finite input is
    // ignored.
    void set_distance(float distance);

    // Moves the camera for one frame of `seconds` around `pivot` and returns the view.
    [[nodiscard]] render::Camera update(const physics::World& world, math::DVec3 pivot, float seconds);

    // The distance it keeps when nothing is in the way.
    [[nodiscard]] float distance() const { return config_.distance; }
    [[nodiscard]] float yaw() const { return yaw_; }
    [[nodiscard]] float pitch() const { return pitch_; }
    // How far the camera is from the pivot now, after any pull-in.
    [[nodiscard]] float current_distance() const { return current_; }
    // The camera's facing along the ground and its right, both horizontal and unit length: what
    // "forward" and "right" mean for movement input.
    [[nodiscard]] math::Vec3 forward() const;
    [[nodiscard]] math::Vec3 right() const;

private:
    // Unit direction from the pivot towards the camera.
    [[nodiscard]] math::Vec3 back() const;
    // The farthest the camera may be along `back`, given what the probes hit.
    [[nodiscard]] float allowed_distance(const physics::World& world, math::DVec3 pivot, math::Vec3 back) const;

    FollowCameraConfig config_;
    float yaw_ = 0.F;
    float pitch_ = 0.3F;
    float current_ = 0.F;
    bool placed_ = false;
};

}
