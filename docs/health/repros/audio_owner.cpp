#include <lykoware/audio.hpp>
#include <array>
#include <iostream>
int main()
{
    namespace audio = lykoware::audio;
    std::string error;
    const std::array<float, 8> samples{0.1F, 0.1F, 0.1F, 0.1F, 0.1F, 0.1F, 0.1F, 0.1F};
    auto sound = audio::Sound::create(samples, 1, 48000, error);
    audio::Mixer a, b;
    audio::PlayParams params;
    params.loop = true;
    auto first = a.play(sound, params, error);
    auto second = b.play(sound, params, error);
    if (!first || !second) return 2;
    const bool accepts_foreign = b.playing(*first);
    const bool stopped_foreign = b.stop(*first);
    const bool victim_stopped = !b.playing(*second);
    std::cout << "equal_ids=" << (*first == *second) << " b_accepts_a_id=" << accepts_foreign
              << " b_stop_a_id=" << stopped_foreign << " b_own_voice_stopped=" << victim_stopped << '\n';
    return accepts_foreign && stopped_foreign && victim_stopped ? 1 : 0;
}
