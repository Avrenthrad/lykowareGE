#include <lykoware/locomotion.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>
#include <stdexcept>
#include <utility>

namespace lykoware::locomotion {
namespace {

using Json = nlohmann::json;

constexpr float pi = std::numbers::pi_v<float>;
constexpr float radians_per_degree = pi / 180.F;
// Below this speed a character counts as standing: a start may go any way at once.
constexpr float standing_speed = 1e-3F;
// Below this wanted speed nothing is wanted.
constexpr float no_request = 1e-4F;

bool fail(std::string& error, std::string text)
{
    error = std::move(text);
    return false;
}

bool within(float value, float low, float high)
{
    return std::isfinite(value) && value >= low && value <= high;
}

bool finite(math::Vec3 v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

float& field(Tuning& tuning, std::size_t index)
{
    float* const fields[]{&tuning.acceleration,   &tuning.braking,     &tuning.pivot_braking, &tuning.turn_rate_rest,
                          &tuning.turn_rate_sprint, &tuning.pivot_angle, &tuning.pivot_speed,   &tuning.facing_rate,
                          &tuning.gait_change_seconds, &tuning.lean};
    return *fields[index];
}

float field(const Tuning& tuning, std::size_t index)
{
    Tuning copy = tuning;
    return field(copy, index);
}

// The allowed range of each Tuning field, by tuning_keys index.
struct Range {
    float low, high;
};
constexpr std::array<Range, 10> tuning_ranges{{
    {0.1F, 200.F},  // acceleration
    {0.1F, 200.F},  // braking
    {0.1F, 200.F},  // pivot_braking
    {1.F, 7200.F},  // turn_rate_rest
    {1.F, 7200.F},  // turn_rate_sprint
    {10.F, 180.F},  // pivot_angle
    {0.F, 20.F},    // pivot_speed
    {1.F, 7200.F},  // facing_rate
    {0.F, 5.F},     // gait_change_seconds
    {0.F, 0.1F},    // lean
}};

bool valid_tuning(const Tuning& tuning, const std::string& what, std::string& error)
{
    for (std::size_t i = 0; i < tuning_keys.size(); ++i) {
        if (!within(field(tuning, i), tuning_ranges[i].low, tuning_ranges[i].high)) {
            return fail(error, what + "." + tuning_keys[i] + " must be from " + std::to_string(tuning_ranges[i].low) +
                                   " to " + std::to_string(tuning_ranges[i].high));
        }
    }
    return true;
}

Tuning blend(const Profile& profile, const Variant& variant)
{
    Tuning out;
    const float t = std::clamp(variant.agility, 0.F, 1.F);
    for (std::size_t i = 0; i < tuning_keys.size(); ++i) {
        const float heavy = field(profile.heavy, i), sharp = field(profile.sharp, i);
        field(out, i) = variant.overrides[i] ? *variant.overrides[i] : heavy + (sharp - heavy) * t;
    }
    return out;
}

const Variant* find(const Profile& profile, std::string_view name)
{
    if (name.empty() || name == profile.base.name) {
        return &profile.base;
    }
    for (const auto& variant : profile.variants) {
        if (variant.name == name) {
            return &variant;
        }
    }
    return nullptr;
}

// FollowCamera's yaw of a horizontal unit direction.
float yaw_of(math::Vec3 direction)
{
    return std::atan2(-direction.x, -direction.z);
}

float wrap(float angle)
{
    return std::remainder(angle, 2.F * pi);
}

// Turns unit direction `from` toward unit direction `to` about +Y by at most `limit` radians.
math::Vec3 turn_toward(math::Vec3 from, math::Vec3 to, float limit)
{
    const float cosine = std::clamp(from.x * to.x + from.z * to.z, -1.F, 1.F);
    const float angle = std::acos(cosine);
    if (angle <= limit) {
        return to;
    }
    // Positive about +Y takes -Z toward -X, as a positive yaw does.
    const float side = from.z * to.x - from.x * to.z;
    const float turn = side < 0.F ? -limit : limit;
    const float c = std::cos(turn), s = std::sin(turn);
    math::Vec3 out{from.x * c + from.z * s, 0.F, -from.x * s + from.z * c};
    const float length = std::sqrt(out.x * out.x + out.z * out.z);
    return {out.x / length, 0.F, out.z / length};
}

float angle_between(math::Vec3 a, math::Vec3 b)
{
    return std::acos(std::clamp(a.x * b.x + a.z * b.z, -1.F, 1.F));
}

}

const char* name(Gait gait)
{
    switch (gait) {
    case Gait::Idle: return "idle";
    case Gait::Walk: return "walk";
    case Gait::Jog: return "jog";
    case Gait::Run: return "run";
    case Gait::Sprint: return "sprint";
    }
    return "unknown";
}

const char* name(Legs legs)
{
    switch (legs) {
    case Legs::Stopped: return "stopped";
    case Legs::Moving: return "moving";
    case Legs::Braking: return "braking";
    case Legs::Pivoting: return "pivoting";
    case Legs::Airborne: return "airborne";
    }
    return "unknown";
}

bool valid(const Profile& profile, std::string& error)
{
    float previous = 0.F;
    for (const float speed : profile.gait_speeds) {
        if (!within(speed, 0.05F, 20.F) || !(speed > previous)) {
            return fail(error, "gait speeds must rise from walk to sprint, each from 0.05 to 20 m/s");
        }
        previous = speed;
    }
    if (!within(profile.gait_margin, 0.F, 0.45F)) {
        return fail(error, "gait_margin must be from 0 to 0.45");
    }
    if (!valid_tuning(profile.heavy, "heavy", error) || !valid_tuning(profile.sharp, "sharp", error)) {
        return false;
    }
    if (!within(profile.air_control, 0.F, 100.F) || !within(profile.max_lean, 0.F, 1.F) ||
        !within(profile.lean_seconds, 0.F, 5.F)) {
        return fail(error, "air_control must be 0..100, max_lean 0..1 and lean_seconds 0..5");
    }
    std::set<std::string_view> names;
    const auto check = [&](const Variant& variant) {
        if (variant.name.empty() || !names.insert(variant.name).second) {
            return fail(error, "locomotion variant names must be unique and not empty");
        }
        if (!within(variant.agility, 0.F, 1.F)) {
            return fail(error, "agility must be from 0 to 1 (" + variant.name + ")");
        }
        const Tuning tuning = blend(profile, variant);
        if (!valid_tuning(tuning, variant.name, error)) {
            return false;
        }
        if (tuning.pivot_speed > profile.gait_speeds[3]) {
            return fail(error, variant.name + ".pivot_speed must not exceed the sprint speed");
        }
        return true;
    };
    if (!check(profile.base)) {
        return false;
    }
    for (const auto& variant : profile.variants) {
        if (!check(variant)) {
            return false;
        }
    }
    error.clear();
    return true;
}

std::optional<Tuning> resolve(const Profile& profile, std::string_view variant)
{
    const Variant* found = find(profile, variant);
    return found ? std::optional<Tuning>{blend(profile, *found)} : std::nullopt;
}

bool decode_profile(std::string_view json, Profile& out, std::string& error)
{
    if (json.size() > 65536) {
        return fail(error, "Locomotion profile exceeds 64 KiB");
    }
    try {
        std::array<std::set<std::string>, 6> keys;
        const auto j = Json::parse(json, [&](int depth, Json::parse_event_t event, Json& value) {
            if (depth > 5) {
                throw std::invalid_argument("Locomotion profile nesting exceeds limit");
            }
            if (event == Json::parse_event_t::object_start) {
                keys[static_cast<std::size_t>(depth)].clear();
            }
            if (event == Json::parse_event_t::key &&
                !keys[static_cast<std::size_t>(depth - 1)].insert(value.get<std::string>()).second) {
                throw std::invalid_argument("Duplicate locomotion profile key");
            }
            return true;
        });
        if (!j.is_object() || !j.contains("version") || !j["version"].is_number_integer() || j["version"] != 1) {
            return fail(error, "Locomotion profile requires version 1");
        }
        const std::set<std::string> allowed{"version",     "agility",  "gaits",        "gait_margin", "heavy",   "sharp",
                                            "overrides",   "variants", "air_control",  "max_lean",    "lean_seconds"};
        for (const auto& item : j.items()) {
            if (!allowed.contains(item.key())) {
                return fail(error, "Unknown locomotion profile key: " + item.key());
            }
        }
        Profile result;
        const auto number = [&](const Json& object, const char* key, float& target) {
            if (!object.contains(key)) {
                return true;
            }
            if (!object[key].is_number()) {
                return fail(error, std::string{"Locomotion profile "} + key + " must be a number");
            }
            target = object[key].get<float>();
            return true;
        };
        // A Tuning object: every key optional, none unknown. Values go to `sink` by index.
        const auto tuning = [&](const Json& object, const std::string& what, const auto& sink) {
            if (!object.is_object()) {
                return fail(error, "Locomotion profile " + what + " must be an object");
            }
            for (const auto& item : object.items()) {
                const auto found = std::find_if(tuning_keys.begin(), tuning_keys.end(),
                                                [&](const char* key) { return item.key() == key; });
                if (found == tuning_keys.end()) {
                    return fail(error, "Unknown locomotion profile key: " + what + "." + item.key());
                }
                if (!item.value().is_number()) {
                    return fail(error, "Locomotion profile " + what + "." + item.key() + " must be a number");
                }
                sink(static_cast<std::size_t>(found - tuning_keys.begin()), item.value().get<float>());
            }
            return true;
        };
        if (!number(j, "agility", result.base.agility) || !number(j, "gait_margin", result.gait_margin) ||
            !number(j, "air_control", result.air_control) || !number(j, "max_lean", result.max_lean) ||
            !number(j, "lean_seconds", result.lean_seconds)) {
            return false;
        }
        if (j.contains("gaits")) {
            const auto& gaits = j["gaits"];
            if (!gaits.is_object()) {
                return fail(error, "Locomotion profile gaits must be an object");
            }
            constexpr std::array<const char*, 4> gait_keys{"walk", "jog", "run", "sprint"};
            for (const auto& item : gaits.items()) {
                if (std::none_of(gait_keys.begin(), gait_keys.end(), [&](const char* key) { return item.key() == key; })) {
                    return fail(error, "Unknown locomotion profile key: gaits." + item.key());
                }
            }
            for (std::size_t i = 0; i < gait_keys.size(); ++i) {
                if (!number(gaits, gait_keys[i], result.gait_speeds[i])) {
                    return false;
                }
            }
        }
        if (j.contains("heavy") &&
            !tuning(j["heavy"], "heavy", [&](std::size_t i, float value) { field(result.heavy, i) = value; })) {
            return false;
        }
        if (j.contains("sharp") &&
            !tuning(j["sharp"], "sharp", [&](std::size_t i, float value) { field(result.sharp, i) = value; })) {
            return false;
        }
        if (j.contains("overrides") && !tuning(j["overrides"], "overrides", [&](std::size_t i, float value) {
                result.base.overrides[i] = value;
            })) {
            return false;
        }
        if (j.contains("variants")) {
            const auto& variants = j["variants"];
            if (!variants.is_object() || variants.size() > 16) {
                return fail(error, "Locomotion profile variants must be an object of at most 16");
            }
            for (const auto& item : variants.items()) {
                const auto& body = item.value();
                if (!body.is_object()) {
                    return fail(error, "Locomotion profile variant must be an object: " + item.key());
                }
                for (const auto& part : body.items()) {
                    if (part.key() != "agility" && part.key() != "overrides") {
                        return fail(error, "Unknown locomotion profile key: variants." + item.key() + "." + part.key());
                    }
                }
                Variant variant;
                variant.name = item.key();
                variant.agility = result.base.agility;
                if (!number(body, "agility", variant.agility)) {
                    return false;
                }
                if (body.contains("overrides") &&
                    !tuning(body["overrides"], "variants." + item.key() + ".overrides",
                            [&](std::size_t i, float value) { variant.overrides[i] = value; })) {
                    return false;
                }
                result.variants.push_back(std::move(variant));
            }
        }
        if (!valid(result, error)) {
            return false;
        }
        out = std::move(result);
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        return fail(error, std::string{"Invalid locomotion profile: "} + exception.what());
    }
}

std::optional<Motor> Motor::create(const Profile& profile, std::string& error)
{
    if (!valid(profile, error)) {
        return std::nullopt;
    }
    Motor motor;
    motor.profile_ = profile;
    motor.retune();
    motor.reset();
    error.clear();
    return motor;
}

void Motor::retune()
{
    tuning_ = blend(profile_, selected_ == 0 ? profile_.base : profile_.variants[selected_ - 1]);
}

void Motor::reset(float facing)
{
    const float yaw = std::isfinite(facing) ? wrap(facing) : 0.F;
    const std::uint64_t pivots = state_.pivots;
    state_ = State{};
    state_.pivots = pivots;
    state_.facing = yaw;
    state_.heading = yaw;
    direction_ = {-std::sin(yaw), 0.F, -std::cos(yaw)};
    gait_seconds_ = 0.F;
    pivoting_ = false;
}

bool Motor::select(std::string_view variant)
{
    if (variant.empty() || variant == profile_.base.name) {
        selected_ = 0;
        retune();
        return true;
    }
    for (std::size_t i = 0; i < profile_.variants.size(); ++i) {
        if (profile_.variants[i].name == variant) {
            selected_ = i + 1;
            retune();
            return true;
        }
    }
    return false;
}

void Motor::set_agility(float agility)
{
    if (!std::isfinite(agility)) {
        return;
    }
    (selected_ == 0 ? profile_.base : profile_.variants[selected_ - 1]).agility = std::clamp(agility, 0.F, 1.F);
    retune();
}

float Motor::agility() const
{
    return (selected_ == 0 ? profile_.base : profile_.variants[selected_ - 1]).agility;
}

float Motor::gait_speed(Gait gait) const
{
    return gait == Gait::Idle ? 0.F : profile_.gait_speeds[static_cast<std::size_t>(gait) - 1];
}

const State& Motor::step(const Request& request, float seconds)
{
    if (!std::isfinite(seconds) || seconds <= 0.F || !finite(request.velocity) ||
        (request.actual_velocity && !finite(*request.actual_velocity))) {
        return state_;
    }
    const float sprint = profile_.gait_speeds[3];
    const math::Vec3 before = state_.velocity;
    float speed = state_.speed;

    // Continue from what physics really gave, when that is slower than was asked for.
    if (request.actual_velocity && request.grounded) {
        const float along = request.actual_velocity->x * direction_.x + request.actual_velocity->z * direction_.z;
        speed = std::min(speed, std::max(along, 0.F));
    }

    math::Vec3 wanted{request.velocity.x, 0.F, request.velocity.z};
    float wanted_speed = std::sqrt(wanted.x * wanted.x + wanted.z * wanted.z);
    math::Vec3 wanted_direction = direction_;
    if (wanted_speed > no_request) {
        wanted_direction = {wanted.x / wanted_speed, 0.F, wanted.z / wanted_speed};
        wanted_speed = std::min(wanted_speed, sprint);
    } else {
        wanted_speed = 0.F;
    }

    Legs legs = Legs::Stopped;
    if (!request.grounded) {
        // In the air: keep the velocity, steer it a little, never brake.
        math::Vec3 velocity{direction_.x * speed, 0.F, direction_.z * speed};
        if (wanted_speed > 0.F) {
            const math::Vec3 change{wanted_direction.x * wanted_speed - velocity.x, 0.F,
                                    wanted_direction.z * wanted_speed - velocity.z};
            const float size = std::sqrt(change.x * change.x + change.z * change.z);
            const float limit = profile_.air_control * seconds;
            const float scale = size > limit ? limit / size : 1.F;
            velocity = {velocity.x + change.x * scale, 0.F, velocity.z + change.z * scale};
        }
        speed = std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
        if (speed > standing_speed) {
            direction_ = {velocity.x / speed, 0.F, velocity.z / speed};
        }
        pivoting_ = false;
        legs = Legs::Airborne;
    } else if (wanted_speed == 0.F) {
        speed = std::max(0.F, speed - tuning_.braking * seconds);
        pivoting_ = false;
        legs = speed > 0.F ? Legs::Braking : Legs::Stopped;
    } else {
        if (speed <= standing_speed) {
            // A standing start goes any way at once: there is no momentum to turn.
            direction_ = wanted_direction;
            pivoting_ = false;
        }
        const float turn_rate = (tuning_.turn_rate_rest +
                                 (tuning_.turn_rate_sprint - tuning_.turn_rate_rest) * std::clamp(speed / sprint, 0.F, 1.F)) *
                                radians_per_degree;
        const float angle = angle_between(direction_, wanted_direction);
        if (!pivoting_ && angle > tuning_.pivot_angle * radians_per_degree && speed > tuning_.pivot_speed) {
            pivoting_ = true;
            ++state_.pivots;
        }
        if (pivoting_) {
            // Plant and pivot: brake along the old heading, then turn on the spot's worth of
            // speed, then push off. The heading does not move while braking.
            if (speed > tuning_.pivot_speed) {
                speed = std::max(tuning_.pivot_speed, speed - tuning_.pivot_braking * seconds);
            } else {
                direction_ = turn_toward(direction_, wanted_direction, turn_rate * seconds);
                if (direction_.x == wanted_direction.x && direction_.z == wanted_direction.z) {
                    pivoting_ = false; // turned all the way: push off
                }
            }
            legs = Legs::Pivoting;
        } else {
            direction_ = turn_toward(direction_, wanted_direction, turn_rate * seconds);
            if (wanted_speed >= speed) {
                speed = std::min(wanted_speed, speed + tuning_.acceleration * seconds);
                legs = Legs::Moving;
            } else {
                speed = std::max(wanted_speed, speed - tuning_.braking * seconds);
                legs = Legs::Braking;
            }
        }
    }

    state_.speed = speed;
    state_.velocity = {direction_.x * speed, 0.F, direction_.z * speed};
    state_.legs = legs;
    state_.heading = yaw_of(direction_);

    // The body turns to face the way it travels, at its own rate, and finishes the turn even if
    // it has stopped meanwhile (as ADR 0064's facing does).
    const float facing_limit = tuning_.facing_rate * radians_per_degree * seconds;
    state_.facing = wrap(state_.facing + std::clamp(wrap(state_.heading - state_.facing), -facing_limit, facing_limit));

    // Gait from speed, with a margin and a shortest stay so it does not flicker. Stopping and
    // starting are never held back.
    const std::array<float, 5> speeds{0.F, profile_.gait_speeds[0], profile_.gait_speeds[1], profile_.gait_speeds[2],
                                      profile_.gait_speeds[3]};
    const auto boundary = [&](std::size_t lower) {
        // Idle ends as soon as there is real movement; the others change at the midpoint.
        return lower == 0 ? profile_.gait_margin * speeds[1] : (speeds[lower] + speeds[lower + 1]) * 0.5F;
    };
    const auto margin = [&](std::size_t lower) {
        return lower == 0 ? profile_.gait_margin * speeds[1] * 0.5F : profile_.gait_margin * (speeds[lower + 1] - speeds[lower]);
    };
    auto gait = static_cast<std::size_t>(state_.gait);
    std::size_t next = gait;
    while (next < 4 && speed > boundary(next) + margin(next)) {
        ++next;
    }
    while (next > 0 && speed < boundary(next - 1) - margin(next - 1)) {
        --next;
    }
    gait_seconds_ = std::min(gait_seconds_ + seconds, 3600.F);
    if (next != gait && (next == 0 || gait == 0 || gait_seconds_ >= tuning_.gait_change_seconds)) {
        state_.gait = static_cast<Gait>(next);
        gait_seconds_ = 0.F;
    }

    // Lean follows the acceleration: forward when speeding up, into a turn when turning.
    float lean_forward = 0.F, lean_side = 0.F;
    if (legs != Legs::Airborne) {
        const math::Vec3 acceleration{(state_.velocity.x - before.x) / seconds, 0.F, (state_.velocity.z - before.z) / seconds};
        const math::Vec3 right{-direction_.z, 0.F, direction_.x};
        lean_forward = std::clamp(tuning_.lean * (acceleration.x * direction_.x + acceleration.z * direction_.z),
                                  -profile_.max_lean, profile_.max_lean);
        lean_side = std::clamp(tuning_.lean * (acceleration.x * right.x + acceleration.z * right.z), -profile_.max_lean,
                               profile_.max_lean);
    }
    const float follow = profile_.lean_seconds > 0.F ? 1.F - std::exp(-seconds / profile_.lean_seconds) : 1.F;
    state_.lean_forward += (lean_forward - state_.lean_forward) * follow;
    state_.lean_side += (lean_side - state_.lean_side) * follow;
    return state_;
}

}
