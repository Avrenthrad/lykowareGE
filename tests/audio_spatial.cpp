// audio.spatial: 3D positional sound on ECS entities (ADR 0034). Links only lykoware::engine and
// needs no audio device: every check compares exact values from a Mixer.

#include <lykoware/audio.hpp>
#include <lykoware/audio_spatial.hpp>
#include <lykoware/ecs.hpp>
#include <lykoware/transform.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace audio = lykoware::audio;
namespace ecs = lykoware::ecs;
using lykoware::Transform;
using lykoware::math::DVec3;

int g_failures = 0;

void expect(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

std::shared_ptr<const audio::Sound> constant(float value, std::size_t frames)
{
    std::string error;
    auto sound = audio::Sound::create(std::vector<float>(frames, value), 1, audio::output_rate, error);
    if (!sound) {
        std::cerr << "FATAL: sound: " << error << '\n';
        std::exit(EXIT_FAILURE);
    }
    return sound;
}

std::vector<float> render(audio::Mixer& mixer, std::size_t frames)
{
    std::vector<float> out(frames * 2);
    mixer.render(out);
    return out;
}

bool same(audio::Placement a, audio::Placement b)
{
    return a.gain == b.gain && a.pan == b.pan;
}

void test_placement()
{
    const audio::Listener origin; // at the origin, facing -Z, +Y up: +X is to the right
    const audio::Falloff falloff{1.F, 100.F};
    expect(same(audio::place({0.0, 0.0, 0.0}, origin, falloff), {1.F, 0.F}), "a source at the listener is full and centred");
    expect(same(audio::place({2.0, 0.0, 0.0}, origin, falloff), {0.5F, 1.F}), "2 m to the right: half volume, fully right");
    expect(same(audio::place({-4.0, 0.0, 0.0}, origin, falloff), {0.25F, -1.F}), "4 m to the left: a quarter, fully left");
    expect(same(audio::place({3.0, 0.0, -4.0}, origin, falloff), {0.2F, 0.6F}), "5 m ahead and right: gain 1/5, pan 3/5");
    expect(same(audio::place({0.0, 0.0, 3.0}, origin, falloff), {1.F / 3.F, 0.F}), "behind pans like in front");
    expect(same(audio::place({0.0, 0.5, 0.0}, origin, falloff), {1.F, 0.F}), "inside the reference distance is full volume");
    expect(audio::place({0.0, 0.0, -99.5}, origin, falloff).gain == 1.F / 99.5F, "just inside max_distance is still heard");
    expect(audio::place({0.0, 0.0, -100.0}, origin, falloff).gain == 0.F, "at max_distance it is silent");

    audio::Listener east;
    east.forward = {1.F, 0.F, 0.F};
    expect(same(audio::place({0.0, 0.0, 2.0}, east, falloff), {0.5F, 1.F}), "facing +X, +Z is to the right");

    const audio::Listener camera = audio::Listener::looking_at({0.0, 7.0, 15.0}, {0.0, 1.5, 0.0});
    expect(audio::place({5.0, 7.0, 15.0}, camera, falloff).pan == 1.F, "a listener looking at a target hears +X on the right");

    // The same scene 16 km away gives the same bits: positions become float only relative to the
    // listener.
    // Values that float cannot hold exactly at 16 km, so rounding to float before subtracting
    // would show.
    const DVec3 far{16000.3, -250.7, -16000.9};
    audio::Listener moved = origin;
    moved.position = far;
    bool exact = true;
    for (const DVec3 p : {DVec3{2.1, 0.3, -0.7}, DVec3{3.3, 1.9, -4.6}, DVec3{-7.7, 0.1, 12.3}, DVec3{0.9, -3.1, 0.5}}) {
        exact = exact && same(audio::place(p, origin, falloff), audio::place({p.x + far.x, p.y + far.y, p.z + far.z}, moved, falloff));
    }
    expect(exact, "a scene 16 km from the origin places sounds exactly as at the origin");
}

struct Scene {
    ecs::World world;
    audio::Mixer mixer;
    audio::SpatialAudio spatial;
    audio::Listener listener;

    ecs::Entity source(DVec3 position, std::shared_ptr<const audio::Sound> sound, bool loop)
    {
        const ecs::Entity entity = world.create();
        world.add<Transform>(entity, Transform{position, {}});
        world.add<audio::AudioSource>(entity, audio::AudioSource{std::move(sound), 1.F, 1.F, loop, {1.F, 100.F}});
        return entity;
    }
};

void test_entities()
{
    Scene scene;
    const ecs::Entity hum = scene.source({2.0, 0.0, 0.0}, constant(0.5F, 64), true);
    std::string error;
    const auto voice = scene.spatial.play(scene.world, hum, scene.listener, scene.mixer, error);
    expect(voice.has_value() && scene.spatial.voices() == 1, "an entity's source starts playing");
    auto out = render(scene.mixer, 1);
    expect(out[0] == 0.F && out[1] == 0.25F, "it starts where the entity is: 2 m right");

    scene.world.get<Transform>(hum)->position = {-4.0, 0.0, 0.0};
    scene.spatial.update(scene.world, scene.listener, scene.mixer);
    out = render(scene.mixer, audio::ramp_frames + 1);
    expect(out[1] == 0.25F && out[0] == 0.F, "a move ramps from the old place");
    expect(out[audio::ramp_frames * 2] == 0.125F && out[audio::ramp_frames * 2 + 1] == 0.F,
           "and arrives at the new one: 4 m left");

    scene.world.destroy(hum);
    scene.spatial.update(scene.world, scene.listener, scene.mixer);
    expect(scene.mixer.active_voices() == 0 && scene.spatial.voices() == 0, "a looping source stops when its entity goes");

    // A one-shot keeps playing from where its entity was, to its end.
    const ecs::Entity knock = scene.source({0.0, 0.0, -2.0}, constant(0.5F, 100), false);
    expect(scene.spatial.play(scene.world, knock, scene.listener, scene.mixer, error).has_value(), "a one-shot starts");
    scene.world.destroy(knock);
    scene.spatial.update(scene.world, scene.listener, scene.mixer);
    out = render(scene.mixer, 50);
    const float centred = 0.5F * (0.5F * std::sqrt(0.5F));
    expect(scene.spatial.voices() == 1 && out[98] == centred && out[99] == centred,
           "a one-shot whose entity went plays on from its last place");
    render(scene.mixer, 100);
    scene.spatial.update(scene.world, scene.listener, scene.mixer);
    expect(scene.spatial.voices() == 0 && scene.mixer.active_voices() == 0, "an ended voice is forgotten");

    // Stopped through the mixer: forgotten too.
    const ecs::Entity loop = scene.source({1.0, 0.0, 0.0}, constant(0.25F, 64), true);
    const auto stopped = scene.spatial.play(scene.world, loop, scene.listener, scene.mixer, error);
    expect(stopped && scene.mixer.stop(*stopped), "a voice can be stopped through the mixer");
    scene.spatial.update(scene.world, scene.listener, scene.mixer);
    expect(scene.spatial.voices() == 0, "a voice stopped through the mixer is forgotten");

    // An impact twice as hard is twice as loud.
    const ecs::Entity hit = scene.source({0.0, 0.0, 0.0}, constant(0.25F, 64), false);
    expect(scene.spatial.play(scene.world, hit, scene.listener, scene.mixer, error, 2.F).has_value(), "gain_scale is accepted");
    out = render(scene.mixer, 1);
    expect(out[0] == 2.F * 0.25F * std::sqrt(0.5F), "gain_scale multiplies the source gain");
}

void test_refusals()
{
    Scene scene;
    std::string error;
    const ecs::Entity bare = scene.world.create();
    scene.world.add<Transform>(bare, Transform{});
    expect(!scene.spatial.play(scene.world, bare, scene.listener, scene.mixer, error) && !error.empty(),
           "an entity without an AudioSource is refused");
    const ecs::Entity no_place = scene.world.create();
    audio::AudioSource unplaced;
    unplaced.sound = constant(0.5F, 8);
    scene.world.add<audio::AudioSource>(no_place, unplaced);
    expect(!scene.spatial.play(scene.world, no_place, scene.listener, scene.mixer, error), "an entity without a Transform is refused");

    const ecs::Entity bad = scene.source({}, constant(0.5F, 8), false);
    scene.world.get<audio::AudioSource>(bad)->falloff = {0.F, 10.F};
    expect(!scene.spatial.play(scene.world, bad, scene.listener, scene.mixer, error), "a zero reference distance is refused");
    scene.world.get<audio::AudioSource>(bad)->falloff = {5.F, 5.F};
    expect(!scene.spatial.play(scene.world, bad, scene.listener, scene.mixer, error), "max_distance must exceed the reference");
    scene.world.get<audio::AudioSource>(bad)->falloff = {1.F, 10.F};
    expect(!scene.spatial.play(scene.world, bad, scene.listener, scene.mixer, error, 17.F), "gain_scale above 16 is refused");
    scene.world.get<audio::AudioSource>(bad)->gain = 4.F;
    expect(!scene.spatial.play(scene.world, bad, scene.listener, scene.mixer, error, 8.F), "a gain product above 16 is refused");
    scene.world.get<audio::AudioSource>(bad)->sound = nullptr;
    expect(!scene.spatial.play(scene.world, bad, scene.listener, scene.mixer, error), "a source without a sound is refused");
    expect(scene.spatial.voices() == 0 && scene.mixer.active_voices() == 0, "refusals start nothing");
}

// Moving sources give the same bits however the output between updates is split.
void test_determinism()
{
    auto script = [](const std::vector<std::size_t>& pieces) {
        Scene scene;
        std::string error;
        const ecs::Entity a = scene.source({-3.0, 0.0, -2.0}, constant(0.3F, 97), true);
        const ecs::Entity b = scene.source({4.0, 1.0, 1.0}, constant(-0.2F, 61), true);
        scene.spatial.play(scene.world, a, scene.listener, scene.mixer, error);
        scene.spatial.play(scene.world, b, scene.listener, scene.mixer, error, 1.5F);
        std::vector<float> all;
        for (int step = 0; step < 20; ++step) {
            scene.world.get<Transform>(a)->position = {-3.0 + 0.4 * step, 0.0, -2.0};
            scene.world.get<Transform>(b)->position = {4.0 - 0.3 * step, 1.0, 1.0 + 0.2 * step};
            scene.listener.position = {0.0, 0.0, 0.05 * step};
            scene.spatial.update(scene.world, scene.listener, scene.mixer);
            for (const std::size_t frames : pieces) {
                const std::vector<float> out = render(scene.mixer, frames);
                all.insert(all.end(), out.begin(), out.end());
            }
        }
        return all;
    };
    expect(script({160}) == script({17, 100, 43}), "moving sources give bit-identical output however rendering is split");
}

}

int main()
{
    test_placement();
    test_entities();
    test_refusals();
    test_determinism();
    if (g_failures != 0) {
        std::cerr << g_failures << " spatial audio assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "spatial audio passed\n";
    return EXIT_SUCCESS;
}
