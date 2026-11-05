// audio.mixer: Lykoware's audio mixer and WAV reader (ADR 0032). Links only lykoware::engine
// and needs no audio device: every check compares exact sample values.

#include <lykoware/audio.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace audio = lykoware::audio;

int g_failures = 0;

void expect(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

std::shared_ptr<const audio::Sound> make(const std::vector<float>& samples, std::uint32_t channels = 1,
                                         std::uint32_t rate = audio::output_rate)
{
    std::string error;
    auto sound = audio::Sound::create(samples, channels, rate, error);
    if (!sound) {
        std::cerr << "FATAL: sound: " << error << '\n';
        std::exit(EXIT_FAILURE);
    }
    return sound;
}

audio::VoiceId start(audio::Mixer& mixer, std::shared_ptr<const audio::Sound> sound, audio::PlayParams params = {})
{
    std::string error;
    const auto voice = mixer.play(std::move(sound), params, error);
    if (!voice) {
        std::cerr << "FATAL: play: " << error << '\n';
        std::exit(EXIT_FAILURE);
    }
    return *voice;
}

std::vector<float> render(audio::Mixer& mixer, std::size_t frames)
{
    std::vector<float> out(frames * 2);
    mixer.render(out);
    return out;
}

void test_sound_validation()
{
    std::string error;
    expect(!audio::Sound::create(std::vector<float>{0.F}, 3, 48000, error), "three channels are refused");
    expect(!audio::Sound::create(std::vector<float>{0.F}, 1, 7999, error), "a rate below 8 kHz is refused");
    expect(!audio::Sound::create(std::vector<float>{0.F}, 1, 192001, error), "a rate above 192 kHz is refused");
    expect(!audio::Sound::create(std::vector<float>{}, 1, 48000, error), "an empty sound is refused");
    expect(!audio::Sound::create(std::vector<float>{0.F, 0.F, 0.F}, 2, 48000, error),
           "stereo with an odd sample count is refused");
    expect(!audio::Sound::create(std::vector<float>{0.F, std::nanf("")}, 1, 48000, error), "a NaN sample is refused");
    expect(!audio::Sound::create(std::vector<float>{1.5F}, 1, 48000, error), "a sample above 1 is refused");
    const auto ok = audio::Sound::create(std::vector<float>{0.F, 0.5F, -0.5F, 1.F}, 2, 44100, error);
    expect(ok && ok->channels() == 2 && ok->frames() == 2 && ok->sample_rate() == 44100, "a valid sound is made");
}

void test_panning_and_mixing()
{
    audio::Mixer mixer;
    const auto half = make(std::vector<float>(64, 0.5F));
    start(mixer, half);
    auto out = render(mixer, 4);
    const float centre = 0.5F * std::sqrt(0.5F);
    expect(out[0] == centre && out[1] == centre, "a centred mono voice has equal power in both channels");

    audio::Mixer left;
    start(left, half, {1.F, -1.F, 1.F, false});
    out = render(left, 2);
    expect(out[0] == 0.5F && out[1] == 0.F, "pan -1 is fully left");
    audio::Mixer right;
    start(right, half, {1.F, 1.F, 1.F, false});
    out = render(right, 2);
    expect(out[0] == 0.F && out[1] == 0.5F, "pan 1 is fully right");

    audio::Mixer two;
    start(two, half, {1.F, -1.F, 1.F, true});
    start(two, make(std::vector<float>(64, 0.25F)), {2.F, -1.F, 1.F, true});
    out = render(two, 2);
    expect(out[0] == 1.F && out[1] == 0.F, "voices add, each scaled by its gain");
    start(two, half, {1.F, -1.F, 1.F, true});
    out = render(two, 2);
    expect(out[0] == 1.F, "the mix is clamped to 1");
    expect(two.set_master_gain(0.25F) && !two.set_master_gain(16.5F), "the master gain is 0..16");
    out = render(two, audio::ramp_frames + 1);
    const float mid = out[audio::ramp_frames];
    expect(out[0] == 1.F && mid > 0.375F && mid < 1.F, "a master gain change ramps instead of jumping");
    expect(out[audio::ramp_frames * 2] == 0.375F, "the master gain scales the whole mix before clamping");

    // A stereo sound is balanced, not panned: centred, each channel keeps its full level.
    audio::Mixer stereo;
    start(stereo, make({0.5F, -0.25F, 0.5F, -0.25F}, 2), {1.F, 0.5F, 1.F, false});
    out = render(stereo, 1);
    expect(out[0] == 0.25F && out[1] == -0.25F, "stereo balance turns down the far channel only");
}

void test_end_and_handles()
{
    audio::Mixer mixer;
    const auto short_sound = make(std::vector<float>(10, 0.5F));
    const audio::VoiceId voice = start(mixer, short_sound, {1.F, -1.F, 1.F, false});
    auto out = render(mixer, 12);
    expect(out[18] == 0.5F, "frame 9, the last, is played");
    expect(out[20] == 0.F && out[22] == 0.F, "after its last frame a voice is silent");
    expect(!mixer.playing(voice) && mixer.active_voices() == 0, "a voice that reaches its end stops");
    expect(!mixer.stop(voice) && !mixer.set_gain(voice, 0.5F), "a finished voice's handle is invalid");

    const audio::VoiceId a = start(mixer, short_sound);
    expect(a.index == voice.index && a.generation != voice.generation, "a free slot is reused with a new generation");
    expect(!mixer.playing(voice) && mixer.playing(a), "the old handle never reaches the new voice");
    expect(mixer.stop(a) && !mixer.playing(a) && !mixer.stop(a), "a voice can be stopped once");
    expect(!mixer.playing(audio::VoiceId{}), "the default handle is never valid");

    std::string error;
    expect(!mixer.play(nullptr, {}, error), "playing no sound is refused");
    expect(!mixer.play(short_sound, {17.F, 0.F, 1.F, false}, error), "gain above 16 is refused");
    expect(!mixer.play(short_sound, {1.F, 1.5F, 1.F, false}, error), "pan beyond 1 is refused");
    expect(!mixer.play(short_sound, {1.F, 0.F, 0.1F, false}, error), "pitch below 1/8 is refused");
    expect(!mixer.play(short_sound, {std::nanf(""), 0.F, 1.F, false}, error), "a NaN gain is refused");
    audio::Mixer full;
    for (std::uint32_t i = 0; i < audio::max_voices; ++i) {
        start(full, short_sound, {1.F, 0.F, 1.F, true});
    }
    expect(!full.play(short_sound, {}, error) && !error.empty(), "a 65th voice is refused");
    std::vector<float> odd(3);
    expect(!full.render(odd), "an odd output size is refused");
}

void test_looping_and_resampling()
{
    // A looping 4-frame ramp plays 0, 0.25, 0.5, 0.75, 0, 0.25, ...
    audio::Mixer loop;
    start(loop, make({0.F, 0.25F, 0.5F, 0.75F}), {1.F, -1.F, 1.F, true});
    auto out = render(loop, 10);
    const float expected[10] = {0.F, 0.25F, 0.5F, 0.75F, 0.F, 0.25F, 0.5F, 0.75F, 0.F, 0.25F};
    bool loops = true;
    for (int i = 0; i < 10; ++i) {
        loops = loops && out[static_cast<std::size_t>(i) * 2] == expected[i];
    }
    expect(loops, "a looping voice wraps to its start exactly");

    // A 24 kHz sound at 48 kHz output: each source frame takes two output frames, and the
    // second is halfway to the next frame.
    audio::Mixer resample;
    start(resample, make({0.F, 1.F, 0.F, 0.5F}, 1, 24000), {1.F, -1.F, 1.F, false});
    out = render(resample, 8);
    const float halfway[8] = {0.F, 0.5F, 1.F, 0.5F, 0.F, 0.25F, 0.5F, 0.25F};
    bool interpolates = true;
    for (int i = 0; i < 8; ++i) {
        interpolates = interpolates && out[static_cast<std::size_t>(i) * 2] == halfway[i];
    }
    expect(interpolates, "a lower-rate sound is interpolated linearly");

    // Looping at 24 kHz: between the last frame and the first, interpolation heads back to the
    // first frame, not to silence.
    audio::Mixer seam;
    start(seam, make({1.F, 0.5F}, 1, 24000), {1.F, -1.F, 1.F, true});
    out = render(seam, 6);
    expect(out[0] == 1.F && out[2] == 0.75F && out[4] == 0.5F && out[6] == 0.75F && out[8] == 1.F,
           "a looping voice interpolates across the loop seam");

    // Pitch 2 plays every other frame.
    audio::Mixer fast;
    start(fast, make({0.F, 0.1F, 0.2F, 0.3F, 0.4F, 0.5F}), {1.F, -1.F, 2.F, false});
    out = render(fast, 4);
    expect(out[0] == 0.F && out[2] == 0.2F && out[4] == 0.4F && out[6] == 0.F, "pitch 2 skips every other frame");
}

void test_gain_ramp()
{
    audio::Mixer mixer;
    const audio::VoiceId voice = start(mixer, make(std::vector<float>(1000, 1.F)), {1.F, -1.F, 1.F, true});
    render(mixer, 10);
    expect(mixer.set_gain(voice, 0.F), "the gain can change while playing");
    auto out = render(mixer, audio::ramp_frames + 10);
    bool falling = true;
    for (std::uint32_t i = 1; i < audio::ramp_frames; ++i) {
        falling = falling && out[i * 2] < out[(i - 1) * 2];
    }
    expect(out[0] == 1.F && falling, "a gain change ramps down over ramp_frames instead of jumping");
    expect(out[audio::ramp_frames * 2] == 0.F && out[(audio::ramp_frames + 9) * 2] == 0.F,
           "the ramp ends exactly on the new gain");
    expect(!mixer.set_gain(voice, -1.F) && !mixer.set_pan(voice, 2.F), "out-of-range changes are refused");
}

// The same calls give the same bits however the output is split into render() calls.
void test_determinism()
{
    auto script = [](audio::Mixer& mixer, const std::vector<std::size_t>& chunks) {
        const auto tone = make({0.3F, -0.6F, 0.9F, -0.2F, 0.1F}, 1, 22050);
        const auto stereo = make({0.5F, -0.5F, 0.25F, 0.75F}, 2, 44100);
        const audio::VoiceId a = start(mixer, tone, {0.8F, -0.3F, 1.3F, true});
        start(mixer, stereo, {0.6F, 0.4F, 0.9F, true});
        std::vector<float> all;
        std::size_t done = 0;
        bool panned = false;
        for (const std::size_t chunk : chunks) {
            // Both scripts have a call boundary at frame 300; the pan change starts its ramp there.
            if (!panned && done == 300) {
                mixer.set_pan(a, 0.7F);
                panned = true;
            }
            std::vector<float> out(chunk * 2);
            mixer.render(out);
            all.insert(all.end(), out.begin(), out.end());
            done += chunk;
        }
        expect(panned, "the determinism script changes pan part-way through");
        return all;
    };
    audio::Mixer one_call;
    audio::Mixer many_calls;
    const std::vector<float> whole = script(one_call, {300, 700});
    const std::vector<float> pieces = script(many_calls, {1, 299, 7, 93, 128, 256, 216});
    bool same = whole.size() == pieces.size();
    for (std::size_t i = 0; same && i < whole.size(); ++i) {
        same = whole[i] == pieces[i];
    }
    expect(same, "rendering in different-sized pieces gives bit-identical output");
}

// Voices that end in a different order across calls must not change which slot the next voice
// takes: slot order is summing order, so that would change the output's bits.
void test_slot_reuse_ignores_splits()
{
    auto script = [](audio::Mixer& mixer, const std::vector<std::size_t>& chunks) {
        // Slot 0 ends at frame 200 and slot 3 at frame 100; slots 1 and 2 keep playing.
        start(mixer, make(std::vector<float>(200, 0.1F)));
        start(mixer, make({0.3F, 0.7F, 0.1F}), {0.9F, 0.2F, 1.F, true});
        start(mixer, make({-0.6F, 0.2F}, 1, 44100), {1.1F, -0.4F, 1.F, true});
        start(mixer, make(std::vector<float>(100, 0.2F)));
        std::vector<float> all;
        for (const std::size_t chunk : chunks) {
            const std::vector<float> out = render(mixer, chunk);
            all.insert(all.end(), out.begin(), out.end());
        }
        const audio::VoiceId late = start(mixer, make({0.123F, -0.456F, 0.789F}, 1, 32000), {1.3F, 0.1F, 1.F, true});
        const std::vector<float> out = render(mixer, 500);
        all.insert(all.end(), out.begin(), out.end());
        return std::pair{late.index, all};
    };
    audio::Mixer one_call;
    audio::Mixer two_calls;
    const auto [one_index, one_out] = script(one_call, {300});
    const auto [two_index, two_out] = script(two_calls, {150, 150});
    expect(one_index == 0 && two_index == 0, "a new voice takes the lowest free slot");
    expect(one_out == two_out, "voices ending in different calls leave the later mix bit-identical");
}

// --- WAV ------------------------------------------------------------------------------------

void put16(std::vector<std::byte>& out, std::uint32_t value)
{
    out.push_back(static_cast<std::byte>(value & 0xFFU));
    out.push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
}

void put32(std::vector<std::byte>& out, std::uint32_t value)
{
    put16(out, value & 0xFFFFU);
    put16(out, value >> 16U);
}

void tag(std::vector<std::byte>& out, const char (&name)[5])
{
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::byte>(name[i]));
    }
}

// A WAV file with the given format fields and raw sample bytes, plus an odd-sized chunk before
// the data to exercise chunk padding.
std::vector<std::byte> wav(std::uint32_t format, std::uint32_t channels, std::uint32_t rate, std::uint32_t bits,
                           const std::vector<std::byte>& data, bool extensible = false)
{
    std::vector<std::byte> fmt;
    put16(fmt, extensible ? 0xFFFEU : format);
    put16(fmt, channels);
    put32(fmt, rate);
    put32(fmt, rate * channels * bits / 8);
    put16(fmt, channels * bits / 8);
    put16(fmt, bits);
    if (extensible) {
        put16(fmt, 22);
        put16(fmt, bits);
        put32(fmt, 0);
        // The subformat GUID {0000XXXX-0000-0010-8000-00AA00389B71}, XXXX being the format.
        put16(fmt, format);
        for (const unsigned value : {0x00U, 0x00U, 0x00U, 0x00U, 0x10U, 0x00U, 0x80U, 0x00U, 0x00U, 0xAAU, 0x00U,
                                     0x38U, 0x9BU, 0x71U}) {
            fmt.push_back(static_cast<std::byte>(value));
        }
    }
    std::vector<std::byte> file;
    tag(file, "RIFF");
    put32(file, 0); // the RIFF size is not trusted
    tag(file, "WAVE");
    tag(file, "fmt ");
    put32(file, static_cast<std::uint32_t>(fmt.size()));
    file.insert(file.end(), fmt.begin(), fmt.end());
    tag(file, "LIST");
    put32(file, 3);
    file.push_back(std::byte{'a'});
    file.push_back(std::byte{'b'});
    file.push_back(std::byte{'c'});
    file.push_back(std::byte{0}); // pad byte
    tag(file, "data");
    put32(file, static_cast<std::uint32_t>(data.size()));
    file.insert(file.end(), data.begin(), data.end());
    return file;
}

void test_wav()
{
    std::string error;
    std::vector<std::byte> s16;
    for (const std::uint32_t v : {0x0000U, 0x4000U, 0xC000U, 0x8000U}) {
        put16(s16, v);
    }
    auto sound = audio::Sound::load_wav(wav(1, 2, 44100, 16, s16), error);
    expect(sound && sound->channels() == 2 && sound->sample_rate() == 44100 && sound->frames() == 2,
           "a 16-bit stereo WAV loads");
    expect(sound && sound->samples()[0] == 0.F && sound->samples()[1] == 0.5F && sound->samples()[2] == -0.5F
               && sound->samples()[3] == -1.F,
           "16-bit samples become exact floats");

    std::vector<std::byte> s24 = {std::byte{0x00}, std::byte{0x00}, std::byte{0x40},
                                  std::byte{0x00}, std::byte{0x00}, std::byte{0xC0}};
    sound = audio::Sound::load_wav(wav(1, 1, 48000, 24, s24, true), error);
    expect(sound && sound->samples()[0] == 0.5F && sound->samples()[1] == -0.5F,
           "a 24-bit extensible WAV loads, sign-extended");
    std::vector<std::byte> bad_guid = wav(1, 1, 48000, 24, s24, true);
    bad_guid[20 + 39] = std::byte{0x72}; // the GUID's last byte (the fmt body starts at byte 20)
    expect(!audio::Sound::load_wav(bad_guid, error), "an extensible WAV with an unknown subformat GUID is refused");

    std::vector<std::byte> u8 = {std::byte{128}, std::byte{192}, std::byte{0}};
    sound = audio::Sound::load_wav(wav(1, 1, 8000, 8, u8), error);
    expect(sound && sound->samples()[0] == 0.F && sound->samples()[1] == 0.5F && sound->samples()[2] == -1.F,
           "8-bit unsigned samples are centred on 128");

    std::vector<std::byte> f32;
    for (const float v : {0.25F, -0.75F, 1.25F}) {
        std::uint32_t raw = 0;
        static_assert(sizeof(raw) == sizeof(v));
        std::memcpy(&raw, &v, sizeof(v));
        put32(f32, raw);
    }
    sound = audio::Sound::load_wav(wav(3, 1, 96000, 32, f32), error);
    expect(sound && sound->samples()[0] == 0.25F && sound->samples()[1] == -0.75F && sound->samples()[2] == 1.F,
           "32-bit float samples load, overshoot clamped to 1");
    std::vector<std::byte> f32_inf;
    put32(f32_inf, 0x7F800000U); // +infinity
    expect(!audio::Sound::load_wav(wav(3, 1, 48000, 32, f32_inf), error), "an infinite float sample is refused");

    std::vector<std::byte> s32;
    put32(s32, 0x40000000U);
    sound = audio::Sound::load_wav(wav(1, 1, 48000, 32, s32), error);
    expect(sound && sound->samples()[0] == 0.5F, "32-bit integer samples load");

    std::vector<std::byte> bad = wav(1, 1, 48000, 16, s16);
    bad[0] = std::byte{'X'};
    expect(!audio::Sound::load_wav(bad, error) && !error.empty(), "a file without RIFF is refused");
    bad = wav(1, 1, 48000, 16, s16);
    bad.resize(bad.size() - 3);
    expect(!audio::Sound::load_wav(bad, error), "a truncated data chunk is refused");
    expect(!audio::Sound::load_wav(wav(2, 1, 48000, 4, s16), error), "ADPCM is refused");
    expect(!audio::Sound::load_wav(wav(1, 6, 48000, 16, s16), error), "six channels are refused");
    expect(!audio::Sound::load_wav(wav(1, 2, 48000, 16, {std::byte{0}, std::byte{0}}), error),
           "data that is not a whole number of frames is refused");
    expect(!audio::Sound::load_wav(wav(1, 1, 4000, 16, s16), error), "a 4 kHz WAV is refused");
    std::vector<std::byte> no_data;
    tag(no_data, "RIFF");
    put32(no_data, 4);
    tag(no_data, "WAVE");
    expect(!audio::Sound::load_wav(no_data, error), "a WAV without chunks is refused");
}

}

int main()
{
    test_sound_validation();
    test_panning_and_mixing();
    test_end_and_handles();
    test_looping_and_resampling();
    test_gain_ramp();
    test_determinism();
    test_slot_reuse_ignores_splits();
    test_wav();
    if (g_failures != 0) {
        std::cerr << g_failures << " audio assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "audio mixer passed\n";
    return EXIT_SUCCESS;
}
