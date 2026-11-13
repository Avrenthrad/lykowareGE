#include <lykoware/npc.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace lykoware {
namespace {
constexpr float two_pi = 2.F * std::numbers::pi_v<float>;
}

math::Vec3 seek(const math::DVec3& from, const math::DVec3& to, float speed, float arrive_radius)
{
    if (!std::isfinite(speed) || speed <= 0.F || !std::isfinite(arrive_radius) || !std::isfinite(from.x) ||
        !std::isfinite(from.z) || !std::isfinite(to.x) || !std::isfinite(to.z)) {
        return {};
    }
    const double dx = to.x - from.x;
    const double dz = to.z - from.z;
    const double distance = std::hypot(dx, dz);
    if (!std::isfinite(distance) || distance < 1e-6) {
        return {};
    }
    double wanted = static_cast<double>(speed);
    if (arrive_radius > 0.F && distance < static_cast<double>(arrive_radius)) {
        wanted *= distance / static_cast<double>(arrive_radius);
    }
    return {static_cast<float>(dx / distance * wanted), 0.F, static_cast<float>(dz / distance * wanted)};
}

std::unique_ptr<NpcCharacter> NpcCharacter::create(physics::World& world, const NpcConfig& config, std::string& error)
{
    std::optional<locomotion::Motor> motor = locomotion::Motor::create(config.locomotion, error);
    if (!motor) {
        error = "the locomotion profile is not usable: " + error;
        return nullptr;
    }
    const std::optional<physics::CharacterId> character = world.create_character(config.body, error);
    if (!character) {
        return nullptr;
    }
    return std::unique_ptr<NpcCharacter>(new NpcCharacter(world, *character, std::move(*motor)));
}

NpcCharacter::NpcCharacter(physics::World& world, physics::CharacterId character, locomotion::Motor motor)
    : world_{world}
    , character_{character}
    , motor_{std::move(motor)}
    , previous_feet_{state().position}
{
    motor_.reset(facing_);
}

NpcCharacter::~NpcCharacter()
{
    world_.destroy_character(character_);
}

void NpcCharacter::want(math::Vec3 velocity, std::optional<float> facing)
{
    if (!std::isfinite(velocity.x) || !std::isfinite(velocity.y) || !std::isfinite(velocity.z) ||
        (facing && !std::isfinite(*facing))) {
        return;
    }
    wanted_ = velocity;
    look_ = facing;
}

void NpcCharacter::before_step(float seconds)
{
    const std::optional<physics::CharacterState> now = world_.character_state(character_);
    // How fast the feet really moved in the latest step: the world reports the velocity it was
    // asked for, not the one a wall or a slope allowed.
    std::optional<math::Vec3> moved;
    if (now && previous_seconds_ > 0.F) {
        moved = math::Vec3{static_cast<float>((now->position.x - previous_feet_.x) / static_cast<double>(previous_seconds_)), 0.F,
                           static_cast<float>((now->position.z - previous_feet_.z) / static_cast<double>(previous_seconds_))};
    }
    previous_feet_ = state().position;
    previous_facing_ = facing_;
    previous_seconds_ = std::isfinite(seconds) && seconds > 0.F ? seconds : 0.F;

    locomotion::Request request;
    request.velocity = wanted_;
    request.grounded = now && now->ground == physics::GroundState::OnGround;
    request.actual_velocity = moved;
    request.facing = look_;
    const locomotion::State& driven = motor_.step(request, seconds);
    facing_ = driven.facing;
    world_.set_character_input(character_, driven.velocity, 0.F);
}

physics::CharacterState NpcCharacter::state() const
{
    return world_.character_state(character_).value_or(physics::CharacterState{});
}

math::DVec3 NpcCharacter::drawn_feet(float alpha) const
{
    const math::DVec3 now = state().position;
    if (!std::isfinite(alpha)) {
        return now;
    }
    const double t = static_cast<double>(std::clamp(alpha, 0.F, 1.F));
    return {previous_feet_.x + (now.x - previous_feet_.x) * t, previous_feet_.y + (now.y - previous_feet_.y) * t,
            previous_feet_.z + (now.z - previous_feet_.z) * t};
}

float NpcCharacter::drawn_facing(float alpha) const
{
    if (!std::isfinite(alpha)) {
        return facing_;
    }
    const float delta = std::remainder(facing_ - previous_facing_, two_pi);
    return std::remainder(previous_facing_ + std::clamp(alpha, 0.F, 1.F) * delta, two_pi);
}

bool NpcCharacter::select_locomotion(std::string_view variant)
{
    return motor_.select(variant);
}

bool NpcCharacter::set_agility(float agility)
{
    if (!std::isfinite(agility)) {
        return false;
    }
    motor_.set_agility(agility);
    return true;
}

}
