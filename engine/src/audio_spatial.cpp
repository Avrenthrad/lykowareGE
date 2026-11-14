#include <lykoware/audio_spatial.hpp>

#include <lykoware/transform.hpp>

#include <algorithm>
#include <cmath>

namespace lykoware::audio {

namespace {

bool finite_in(float value, float low, float high)
{
    return std::isfinite(value) && value >= low && value <= high;
}

bool valid(const Falloff& falloff)
{
    return std::isfinite(falloff.reference_distance) && std::isfinite(falloff.max_distance)
        && falloff.reference_distance > 0.F && falloff.max_distance > falloff.reference_distance;
}

}

Listener Listener::looking_at(math::DVec3 eye, math::DVec3 target, math::Vec3 up)
{
    return Listener{eye, math::relative_to(target, eye), up};
}

Placement place(math::DVec3 position, const Listener& listener, const Falloff& falloff)
{
    // Relative to the listener in double, then float: the same at any distance from the origin.
    const math::Vec3 offset = math::relative_to(position, listener.position);
    const float distance = math::length(offset);
    const math::Vec3 right = math::normalize(math::cross(listener.forward, listener.up));
    Placement result;
    if (distance > 0.F) {
        const float pan = math::dot(offset, right) / distance;
        result.pan = std::isfinite(pan) ? std::clamp(pan, -1.F, 1.F) : 0.F;
    }
    // The comparison is false for NaN, which is silent too.
    if (distance < falloff.max_distance) {
        result.gain = falloff.reference_distance / std::max(distance, falloff.reference_distance);
    }
    return result;
}

std::optional<VoiceId> SpatialAudio::play(const ecs::World& world, ecs::Entity entity, const Listener& listener,
                                          Mixer& mixer, std::string& error, float gain_scale)
{
    const Transform* transform = world.get<Transform>(entity);
    const AudioSource* source = world.get<AudioSource>(entity);
    if (transform == nullptr || source == nullptr) {
        error = "the entity needs a Transform and an AudioSource";
        return std::nullopt;
    }
    if (!valid(source->falloff)) {
        error = "the falloff needs 0 < reference_distance < max_distance";
        return std::nullopt;
    }
    const float gain = source->gain * gain_scale;
    if (!finite_in(gain_scale, 0.F, 16.F) || !finite_in(source->gain, 0.F, 16.F) || !finite_in(gain, 0.F, 16.F)) {
        error = "the source gain times gain_scale must be 0..16";
        return std::nullopt;
    }
    const Placement placement = place(transform->position, listener, source->falloff);
    const auto voice = mixer.play_nearest(source->sound, {gain * placement.gain, placement.pan, source->pitch, source->loop},
        std::hypot(transform->position.x - listener.position.x, transform->position.y - listener.position.y,
                   transform->position.z - listener.position.z), error);
    if (!voice) {
        return std::nullopt;
    }
    voices_.push_back({entity, *voice, gain, source->falloff, source->loop, transform->position});
    return voice;
}

void SpatialAudio::update(const ecs::World& world, const Listener& listener, Mixer& mixer)
{
    std::size_t kept = 0;
    for (Tracked& tracked : voices_) {
        if (!mixer.playing(tracked.voice)) {
            continue; // it ended, or was stopped through the mixer
        }
        const Transform* transform = world.get<Transform>(tracked.entity);
        const bool present = transform != nullptr && world.has<AudioSource>(tracked.entity);
        if (!present && tracked.loop) {
            mixer.stop(tracked.voice);
            continue;
        }
        if (present) {
            tracked.last_position = transform->position;
        }
        const Placement placement = place(tracked.last_position, listener, tracked.falloff);
        mixer.set_distance(tracked.voice, std::hypot(tracked.last_position.x - listener.position.x,
            tracked.last_position.y - listener.position.y, tracked.last_position.z - listener.position.z));
        mixer.set_gain(tracked.voice, tracked.gain * placement.gain);
        mixer.set_pan(tracked.voice, placement.pan);
        voices_[kept++] = tracked;
    }
    voices_.resize(kept);
}

}
