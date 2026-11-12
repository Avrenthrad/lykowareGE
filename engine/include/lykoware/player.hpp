#pragma once

#include <lykoware/follow_camera.hpp>
#include <lykoware/input.hpp>
#include <lykoware/locomotion.hpp>
#include <lykoware/math.hpp>
#include <lykoware/physics.hpp>
#include <lykoware/render.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>

// A player-controlled character: input becomes camera-relative movement, jumps and a follow
// camera's turns (ADR 0047).

namespace lykoware {

// How input turns into a player's movement and view.
struct PlayerInputConfig {
    // Radians the view turns per pixel of mouse motion.
    float mouse_radians_per_pixel = 0.0025F;
    // Radians per second the view turns with the right stick or an arrow key held fully.
    float turn_radians_per_second = 2.5F;
    // Stick deflection up to this is ignored; the rest of the range is rescaled to 0..1.
    // Clamped to 0..0.9.
    float stick_dead_zone = 0.2F;
    // Mouse, stick and arrow keys pointing up look down instead of up.
    bool invert_pitch = false;
};

// What a player asks for in one frame.
struct PlayerIntent {
    // Movement along the camera's forward and right, each -1..1; together of length at most 1.
    float forward = 0.F;
    float right = 0.F;
    // How far to turn the view this frame, in radians, in FollowCamera::turn's convention.
    float yaw = 0.F;
    float pitch = 0.F;
    // Zoom steps this frame: positive moves the camera in, towards first person.
    float zoom = 0.F;
    bool jump = false; // pressed this frame
    bool run = false;  // held
    bool walk = false; // held: go no faster than a walk (used only with a locomotion profile)
};

// One frame's intent from keyboard, mouse and gamepad together:
//   move: W A S D, or the left stick;    run: Shift, or the left stick pressed in
//   walk: Ctrl
//   look: mouse motion (only when `mouse_look`), the arrow keys, or the right stick
//   jump: Space, or the South face button
//   zoom: the mouse wheel, or the d-pad up (in) and down (out)
// `seconds` is the frame's length, for the stick and arrow-key turn rate; a non-finite or
// negative length turns nothing. Pass `mouse_look` only while the window holds the mouse in
// relative mode, so a free cursor crossing the window does not turn the view.
[[nodiscard]] PlayerIntent read_player_intent(const InputState& input, const PlayerInputConfig& config, float seconds,
                                              bool mouse_look);

struct PlayerConfig {
    // The character's capsule, spawn point (its feet), walkable slope and step height.
    physics::CharacterDesc body;
    // Speeds in metres per second.
    float walk_speed = 4.F;
    float run_speed = 7.F;
    float jump_speed = 5.F;
    // How long a jump pressed in the air is kept for walkable ground, in seconds.
    float jump_buffer = 0.1F;
    // The camera's pivot above the feet, in metres. It must be inside the capsule, below its top.
    float eye_height = 1.6F;
    FollowCameraConfig camera;
    // Zoom: each step moves the camera this far, up to max_distance. Closer than one step it
    // goes to first person (distance 0), so it never sits inside the head.
    float zoom_step = 1.F;
    float max_distance = 8.F;
    // Visual yaw only, radians/second. 0 preserves legacy instant facing.
    float max_turn_rate = 0.F;
    // Movement by the locomotion motor (ADR 0074). Without a profile the character moves as
    // before: at walk_speed or run_speed at once. With one, walk_speed, run_speed and
    // max_turn_rate are unused: a full push asks for the run gait's speed, `run` for the
    // sprint gait's, `walk` for the walk gait's, and a partial stick push for part of it; the
    // motor decides how the character gets there.
    std::optional<locomotion::Profile> locomotion;
};

// A character in a physics world that a player steers, with a follow camera.
//
// Each frame: steer() with that frame's intent, then before_step() before each fixed physics
// step that frame (zero or more), then view() for the camera to draw with. Movement is relative
// to where the camera faces, so "forward" is always into the screen.
//
// Drawing between steps (ADR 0048): physics steps at a fixed rate, frames come faster or slower.
// Pass the frame's FrameSteps::alpha to view() and drawn_feet() and the character and camera are
// drawn that far between the last two steps, so they move smoothly at any frame rate. This is
// presentation only: alpha never reaches the simulation.
//
// Ownership: create() returns the sole owner. It owns its character in the world and destroys it
// with itself; the world must outlive it. Copy and move are not provided.
//
// Threading: one thread at a time, with no physics step running.
//
// Determinism: the same intents and steps in the same order give bit-identical characters.
class PlayerCharacter {
public:
    // Creates the character at config.body.position. nullptr, with error filled, when the world
    // refuses the character or the config is unusable (non-finite or negative speeds, an eye
    // height outside the capsule).
    [[nodiscard]] static std::unique_ptr<PlayerCharacter> create(physics::World& world, const PlayerConfig& config,
                                                                 std::string& error);

    ~PlayerCharacter();
    PlayerCharacter(const PlayerCharacter&) = delete;
    PlayerCharacter& operator=(const PlayerCharacter&) = delete;
    PlayerCharacter(PlayerCharacter&&) = delete;
    PlayerCharacter& operator=(PlayerCharacter&&) = delete;

    // Takes one frame's intent: the view turns and zooms now; movement and a jump take effect
    // from the next before_step(). Non-finite parts are ignored.
    void steer(const PlayerIntent& intent);
    // Sets the character's input for the coming physics step of `seconds`: the latest intent's
    // movement relative to the camera, and a jump if one is waiting and the character stands on
    // walkable ground.
    void before_step(float seconds);
    // The camera for this frame, orbiting the drawn pivot (drawn_feet(alpha) plus eye_height);
    // `seconds` is the frame's length, for the camera easing back out after an obstruction.
    // alpha 1, the default, is the latest step.
    [[nodiscard]] render::Camera view(float seconds, float alpha = 1.F);
    // Where to draw the feet: `alpha` (clamped to 0..1) of the way from where they were before
    // the latest step to where they are after it. A non-finite alpha gives the latest step.
    [[nodiscard]] math::DVec3 drawn_feet(float alpha) const;

    [[nodiscard]] physics::CharacterId character() const { return character_; }
    [[nodiscard]] physics::CharacterState state() const;
    // Where the camera orbits: the feet plus eye_height.
    [[nodiscard]] math::DVec3 pivot() const;
    // The way the body faces, as a yaw (FollowCamera's convention): the last way it was moved.
    [[nodiscard]] float facing() const { return facing_; }
    [[nodiscard]] float drawn_facing(float alpha) const;
    [[nodiscard]] const FollowCamera& camera() const { return camera_; }
    // The locomotion motor's state after the latest before_step(), or nullptr without a
    // locomotion profile.
    [[nodiscard]] const locomotion::State* locomotion() const { return motor_ ? &motor_->state() : nullptr; }
    // Selects a variant of the locomotion profile by name (empty for the base), or sets the
    // selected variant's agility. False without a profile or for an unknown variant.
    bool select_locomotion(std::string_view variant);
    bool set_agility(float agility);

private:
    PlayerCharacter(physics::World& world, physics::CharacterId character, const PlayerConfig& config,
                    std::optional<locomotion::Motor> motor);

    physics::World& world_;
    physics::CharacterId character_;
    PlayerConfig config_;
    FollowCamera camera_;
    PlayerIntent intent_{};
    std::optional<locomotion::Motor> motor_;
    float jump_wait_ = 0.F;
    float facing_ = 0.F;
    float target_facing_ = 0.F, previous_facing_ = 0.F;
    // The feet before the latest step, for drawing between steps.
    math::DVec3 previous_feet_;
    // The length of the latest step, to know how fast the feet really moved in it.
    float previous_seconds_ = 0.F;
};

}
