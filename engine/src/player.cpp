#include <lykoware/player.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <cstddef>

namespace lykoware {
namespace {

bool held(const InputState& input, Key key)
{
    return input.keys[static_cast<std::size_t>(key)].down;
}

bool held(const InputState& input, GamepadButton button)
{
    return input.gamepad.buttons[static_cast<std::size_t>(button)].down;
}

bool pressed(const InputState& input, GamepadButton button)
{
    return input.gamepad.buttons[static_cast<std::size_t>(button)].pressed;
}

float axis(const InputState& input, GamepadAxis which)
{
    const float value = input.gamepad.axes[static_cast<std::size_t>(which)];
    return std::isfinite(value) ? std::clamp(value, -1.F, 1.F) : 0.F;
}

float either(bool positive, bool negative)
{
    return (positive ? 1.F : 0.F) - (negative ? 1.F : 0.F);
}

// A stick with a radial dead zone: nothing up to `dead_zone`, then the rest rescaled to 0..1, so
// small deflections are fine control rather than a jump to `dead_zone` speed.
math::Vec2 stick(const InputState& input, GamepadAxis x, GamepadAxis y, float dead_zone)
{
    const float sx = axis(input, x);
    const float sy = axis(input, y);
    const float length = std::sqrt(sx * sx + sy * sy);
    if (length <= dead_zone) {
        return {};
    }
    const float scale = std::min(1.F, (length - dead_zone) / (1.F - dead_zone)) / length;
    return {sx * scale, sy * scale};
}

// Shortens a vector longer than 1 to length 1.
math::Vec2 at_most_one(math::Vec2 v)
{
    const float length = std::sqrt(v.x * v.x + v.y * v.y);
    return length > 1.F ? math::Vec2{v.x / length, v.y / length} : v;
}

float finite_or_zero(float value)
{
    return std::isfinite(value) ? value : 0.F;
}

}

PlayerIntent read_player_intent(const InputState& input, const PlayerInputConfig& config, float seconds, bool mouse_look)
{
    const float dead_zone = std::isfinite(config.stick_dead_zone) ? std::clamp(config.stick_dead_zone, 0.F, 0.9F) : 0.F;
    PlayerIntent intent;

    // Stick up is negative Y, and up means forward.
    const math::Vec2 left = stick(input, GamepadAxis::LeftX, GamepadAxis::LeftY, dead_zone);
    const math::Vec2 move = at_most_one({either(held(input, Key::D), held(input, Key::A)) + left.x,
                                         either(held(input, Key::W), held(input, Key::S)) - left.y});
    intent.right = move.x;
    intent.forward = move.y;

    // Looking right turns the yaw negative (FollowCamera: yaw 0 faces -Z, +X is to the right);
    // looking down (mouse or stick moving down, the down arrow) raises the pitch.
    const float flip = config.invert_pitch ? -1.F : 1.F;
    const math::Vec2 right = stick(input, GamepadAxis::RightX, GamepadAxis::RightY, dead_zone);
    const math::Vec2 turn = at_most_one({either(held(input, Key::Right), held(input, Key::Left)) + right.x,
                                         either(held(input, Key::Down), held(input, Key::Up)) + right.y});
    const float rate = std::isfinite(seconds) && seconds > 0.F ? finite_or_zero(config.turn_radians_per_second) * seconds
                                                               : 0.F;
    intent.yaw = -turn.x * rate;
    intent.pitch = flip * turn.y * rate;
    if (mouse_look) {
        const float per_pixel = finite_or_zero(config.mouse_radians_per_pixel);
        intent.yaw -= finite_or_zero(input.mouse.move_x) * per_pixel;
        intent.pitch += flip * finite_or_zero(input.mouse.move_y) * per_pixel;
    }

    intent.zoom = finite_or_zero(input.mouse.wheel_y) + either(pressed(input, GamepadButton::DpadUp),
                                                               pressed(input, GamepadButton::DpadDown));
    intent.jump = input.keys[static_cast<std::size_t>(Key::Space)].pressed || pressed(input, GamepadButton::South);
    intent.run = held(input, Key::LeftShift) || held(input, Key::RightShift) || held(input, GamepadButton::LeftStick);
    return intent;
}

std::unique_ptr<PlayerCharacter> PlayerCharacter::create(physics::World& world, const PlayerConfig& config,
                                                         std::string& error)
{
    error.clear();
    const auto usable_speed = [](float speed) { return std::isfinite(speed) && speed >= 0.F; };
    if (!usable_speed(config.walk_speed) || !usable_speed(config.run_speed) || !usable_speed(config.jump_speed)
        || !usable_speed(config.jump_buffer) || !usable_speed(config.zoom_step) || !usable_speed(config.max_distance) || !usable_speed(config.max_turn_rate)) {
        error = "player speeds, turn rate, jump buffer and zoom must be finite and not negative";
        return nullptr;
    }
    const float height = 2.F * (config.body.cylinder_half_height + config.body.radius);
    if (!std::isfinite(config.eye_height) || config.eye_height <= 0.F || !(config.eye_height < height)) {
        error = "the eye height must be inside the character's capsule, above its feet and below its top";
        return nullptr;
    }
    const std::optional<physics::CharacterId> character = world.create_character(config.body, error);
    if (!character) {
        return nullptr;
    }
    return std::unique_ptr<PlayerCharacter>(new PlayerCharacter(world, *character, config));
}

PlayerCharacter::PlayerCharacter(physics::World& world, physics::CharacterId character, const PlayerConfig& config)
    : world_{world}
    , character_{character}
    , config_{config}
    , camera_{config.camera}
    , facing_{camera_.yaw()}
    , target_facing_{facing_}
    , previous_facing_{facing_}
    , previous_feet_{state().position}
{
    config_.max_distance = std::max(config_.max_distance, camera_.distance());
}

PlayerCharacter::~PlayerCharacter()
{
    world_.destroy_character(character_);
}

void PlayerCharacter::steer(const PlayerIntent& intent)
{
    camera_.turn(finite_or_zero(intent.yaw), finite_or_zero(intent.pitch));

    const float zoom = finite_or_zero(intent.zoom);
    if (zoom != 0.F) {
        const float step = config_.zoom_step;
        float distance = std::clamp(camera_.distance() - zoom * step, 0.F, config_.max_distance);
        // Between first person and one step out the camera would be inside the head.
        if (distance > 0.F && distance < step) {
            distance = zoom > 0.F ? 0.F : step;
        }
        camera_.set_distance(distance);
    }

    intent_.forward = finite_or_zero(intent.forward);
    intent_.right = finite_or_zero(intent.right);
    const math::Vec2 move = at_most_one({intent_.right, intent_.forward});
    intent_.right = move.x;
    intent_.forward = move.y;
    intent_.run = intent.run;
    if (intent.jump) {
        // Kept for a short time, so a press just before landing still jumps.
        jump_wait_ = config_.jump_buffer;
    }
}

void PlayerCharacter::before_step(float seconds)
{
    previous_feet_ = state().position;
    previous_facing_ = facing_;
    const float speed = intent_.run ? config_.run_speed : config_.walk_speed;
    const math::Vec3 velocity = (camera_.forward() * intent_.forward + camera_.right() * intent_.right) * speed;
    if (intent_.forward != 0.F || intent_.right != 0.F) {
        // Yaw of a horizontal direction d is atan2(-d.x, -d.z) (FollowCamera::forward()).
        target_facing_ = std::atan2(-velocity.x, -velocity.z);
    }

    if (config_.max_turn_rate == 0.F) { facing_ = target_facing_; }
    else if (std::isfinite(seconds) && seconds > 0.F) {
        const float delta = std::remainder(target_facing_ - facing_, 2.F * std::numbers::pi_v<float>);
        const double limit = static_cast<double>(config_.max_turn_rate) * seconds;
        const float change = static_cast<float>(std::clamp(static_cast<double>(delta), -limit, limit));
        facing_ = std::remainder(facing_ + change, 2.F * std::numbers::pi_v<float>);
    }

    const std::optional<physics::CharacterState> now = world_.character_state(character_);
    const bool grounded = now && now->ground == physics::GroundState::OnGround;
    float jump = 0.F;
    if (jump_wait_ > 0.F && grounded) {
        jump = config_.jump_speed;
        jump_wait_ = 0.F;
    } else if (std::isfinite(seconds) && seconds > 0.F) {
        jump_wait_ = std::max(0.F, jump_wait_ - seconds);
    }
    // A jump is passed only when the character is on walkable ground, where the world uses it
    // in this step; it is never left waiting in the world to fire on some later landing.
    world_.set_character_input(character_, velocity, jump);
}

physics::CharacterState PlayerCharacter::state() const
{
    return world_.character_state(character_).value_or(physics::CharacterState{});
}

math::DVec3 PlayerCharacter::pivot() const
{
    const math::DVec3 feet = state().position;
    return {feet.x, feet.y + static_cast<double>(config_.eye_height), feet.z};
}

math::DVec3 PlayerCharacter::drawn_feet(float alpha) const
{
    const math::DVec3 now = state().position;
    if (!std::isfinite(alpha)) {
        return now;
    }
    const double t = static_cast<double>(std::clamp(alpha, 0.F, 1.F));
    return {previous_feet_.x + (now.x - previous_feet_.x) * t, previous_feet_.y + (now.y - previous_feet_.y) * t,
            previous_feet_.z + (now.z - previous_feet_.z) * t};
}

float PlayerCharacter::drawn_facing(float alpha) const
{
    if (!std::isfinite(alpha)) { return facing_; }
    const float delta = std::remainder(facing_ - previous_facing_, 2.F * std::numbers::pi_v<float>);
    return std::remainder(previous_facing_ + std::clamp(alpha,0.F,1.F)*delta, 2.F * std::numbers::pi_v<float>);
}

render::Camera PlayerCharacter::view(float seconds, float alpha)
{
    const math::DVec3 feet = drawn_feet(alpha);
    return camera_.update(world_, {feet.x, feet.y + static_cast<double>(config_.eye_height), feet.z}, seconds);
}

}
