#pragma once

#include <lykoware/audio.hpp>
#include <lykoware/ecs.hpp>
#include <lykoware/math.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// 3D positional sound (ADR 0034): sounds played from ECS entities, with volume falling off over
// distance and a left/right pan from where the source is around the listener. Positions are
// taken relative to the listener in double precision before they become float, so a scene
// 16 km from the origin sounds exactly like the same scene at the origin.

namespace lykoware::audio {

// Where the ears are and which way the head faces. forward and up need not be unit length, but
// must not be zero or parallel.
struct Listener {
    math::DVec3 position;
    math::Vec3 forward{0.F, 0.F, -1.F};
    math::Vec3 up{0.F, 1.F, 0.F};

    // A listener at `eye` facing `target`, as for render::Camera.
    [[nodiscard]] static Listener looking_at(math::DVec3 eye, math::DVec3 target, math::Vec3 up = {0.F, 1.F, 0.F});
};

// How a source's volume falls with distance: full volume within reference_distance, then
// inverse distance (half the volume at twice the distance, about 6 dB quieter), and silent
// beyond max_distance. Distances are in metres; 0 < reference_distance < max_distance.
struct Falloff {
    float reference_distance = 1.F;
    float max_distance = 100.F;
};

// The volume (0..1) and pan (-1 left .. 1 right) for a sound at `position`. Behind the listener
// pans like in front (stereo cannot tell them apart). A source at the listener is centred.
struct Placement {
    float gain = 0.F;
    float pan = 0.F;
};
[[nodiscard]] Placement place(math::DVec3 position, const Listener& listener, const Falloff& falloff);

// ECS component: a sound an entity plays from its Transform's position.
struct AudioSource {
    std::shared_ptr<const Sound> sound;
    // 0..16, before distance.
    float gain = 1.F;
    // 1/8..8.
    float pitch = 1.F;
    bool loop = false;
    Falloff falloff;
};

// Plays entities' AudioSources and keeps each voice where its entity is.
//
// Every call takes the ECS world, the listener and the Mixer (for an audio::Output, the mixer
// from Output::lock()). Voices are updated in the order they were started, so the same calls
// give the same output (ADR 0032).
//
// Entities that go away: a looping voice stops at the next update(); a one-shot voice plays to
// its end from the entity's last position.
//
// Threading: like Mixer, one thread at a time.
class SpatialAudio {
public:
    // Starts the entity's AudioSource where its Transform is. gain_scale (0..16) multiplies
    // AudioSource::gain, e.g. for how hard an impact was; the product must stay within 16.
    // Fails (nullopt, error filled) when the entity has no Transform or AudioSource, the falloff
    // or gain is out of range, or the mixer refuses the voice.
    std::optional<VoiceId> play(const ecs::World& world, ecs::Entity entity, const Listener& listener, Mixer& mixer,
                                std::string& error, float gain_scale = 1.F);

    // Once per frame, after the simulation: moves every voice's volume and pan to where its
    // entity is now (ramped by the mixer, so moving sources do not click), forgets voices that
    // have ended, and stops looping voices whose entity or AudioSource is gone.
    void update(const ecs::World& world, const Listener& listener, Mixer& mixer);

    // Voices being kept in place.
    [[nodiscard]] std::size_t voices() const { return voices_.size(); }

private:
    struct Tracked {
        ecs::Entity entity;
        VoiceId voice;
        float gain = 1.F;
        Falloff falloff;
        bool loop = false;
        math::DVec3 last_position;
    };

    std::vector<Tracked> voices_;
};

}
