#include <lykoware/audio.hpp>
#include <lykoware/audio_spatial.hpp>
#include <lykoware/audio_stream.hpp>
#include <lykoware/transform.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
namespace audio = lykoware::audio;
void require(bool condition, const char* message)
{
    if (!condition) { throw std::runtime_error(message); }
}

void priority()
{
    std::string error;
    const std::array<float, 1> samples{0.25F};
    auto sound = audio::Sound::create(samples, 1, audio::output_rate, error);
    const audio::PlayParams loop{1.F, 0.F, 1.F, true};
    audio::Mixer mixer;
    std::vector<audio::VoiceId> ids;
    for (std::size_t i = 0; i < audio::max_voices; ++i) {
        auto id = mixer.play_nearest(sound, loop, 100.0 + static_cast<double>(i), error);
        require(id.has_value(), "fill priority voices");
        ids.push_back(*id);
    }
    require(!mixer.play_nearest({}, loop, 0.0, error), "invalid sound cannot steal");
    require(!mixer.play_nearest(sound, loop, -1.0, error), "negative distance refused");
    require(!mixer.play_nearest(sound, loop, std::numeric_limits<double>::infinity(), error), "infinity refused");
    auto near = mixer.play_nearest(sound, loop, 1.0, error);
    require(near && mixer.playing(*near), "near voice admitted at saturation");
    require(!mixer.playing(ids.back()) && !mixer.stop(ids.back()), "farthest handle invalidated");
    require(mixer.playing(ids.front()) && mixer.active_voices() == audio::max_voices, "nearer existing voice retained");
    require(!mixer.play_nearest(sound, loop, 999.0, error), "farther arrival refused");
    require(mixer.set_distance(ids.front(), 1000.0), "movement updates priority");
    auto moved = mixer.play_nearest(sound, loop, 2.0, error);
    require(moved && !mixer.playing(ids.front()), "updated farthest is replaced");

    audio::Mixer protected_mixer;
    for (std::size_t i = 0; i < audio::max_voices; ++i) {
        require(protected_mixer.play(sound, loop, error).has_value(), "fill legacy voices");
    }
    require(!protected_mixer.play_nearest(sound, loop, 0.0, error), "legacy voices protected");

    audio::Mixer ties;
    for (std::size_t i = 0; i < audio::max_voices; ++i) {
        require(ties.play_nearest(sound, loop, 10.0, error).has_value(), "fill equal distances");
    }
    require(!ties.play_nearest(sound, loop, 10.0, error), "equal arrival refused");
    auto tie = ties.play_nearest(sound, loop, 1.0, error);
    require(tie && tie->index == 0, "equal victim distances choose lowest slot");

    lykoware::ecs::World world;
    audio::SpatialAudio spatial;
    audio::Mixer spatial_mixer;
    audio::Listener listener;
    listener.position.x = 16000.3;
    auto entity = world.create();
    world.add<lykoware::Transform>(entity, lykoware::Transform{{16080.3, 0.0, 0.0}, {}});
    audio::AudioSource source;
    source.sound = sound;
    source.loop = true;
    world.add<audio::AudioSource>(entity, source);
    std::optional<audio::VoiceId> first;
    for (std::size_t i = 0; i < audio::max_voices; ++i) {
        auto id = spatial.play(world, entity, listener, spatial_mixer, error);
        require(id.has_value(), "fill spatial mixer");
        if (i == 0) { first = id; }
    }
    world.get<lykoware::Transform>(entity)->position.x = 16001.3;
    auto closer = spatial.play(world, entity, listener, spatial_mixer, error);
    require(closer && !spatial_mixer.playing(*first), "spatial uses listener distance");
    spatial.update(world, listener, spatial_mixer);
    require(spatial.voices() == audio::max_voices, "spatial forgets stolen handles");
}

void streaming(const std::filesystem::path& root)
{
    std::filesystem::create_directories(root);
    const auto path = root / "clip.pcm";
    constexpr std::size_t frames = 4097;
    std::vector<float> expected;
    {
        std::ofstream file(path, std::ios::binary);
        for (std::size_t i = 0; i < frames * 2; ++i) {
            const int value = static_cast<int>(i % 65536) - 32768;
            expected.push_back(static_cast<float>(value) / 32768.F);
            const auto bits = static_cast<unsigned int>(value < 0 ? value + 65536 : value);
            file.put(static_cast<char>(bits & 255U));
            file.put(static_cast<char>(bits >> 8U));
        }
    }
    std::string error;
    require(!audio::PcmStream::open(path, 0, error), "zero capacity refused");
    require(!audio::PcmStream::open(path, 48001, error), "unbounded capacity refused");
    require(!audio::PcmStream::open(root / "missing.pcm", 7, error), "missing stream refused");
    auto stream = audio::PcmStream::open(path, 7, error);
    require(stream && stream->buffer_bytes() <= 7 * 2 * sizeof(float), "bounded allocation");
    audio::Mixer mixer;
    auto voice = mixer.play_stream(stream, {}, error);
    require(voice.has_value(), "start unpumped stream");
    require(!mixer.play_stream(stream, {}, error), "duplicate consumer refused");
    std::array<float, 6> silence{1.F, 1.F, 1.F, 1.F, 1.F, 1.F};
    require(mixer.render(silence) && silence == std::array<float, 6>{}, "starvation emits silence");
    require(mixer.playing(*voice), "starvation keeps source position");
    std::vector<float> actual;
    while (!stream->finished()) {
        require(stream->pump(error) && stream->buffered_frames() <= 7, "bounded refill");
        const auto count = stream->buffered_frames();
        std::vector<float> out(count * 2);
        require(mixer.render(out), "render stream");
        actual.insert(actual.end(), out.begin(), out.end());
    }
    require(actual == expected, "stream samples match across ring wrap and EOF");
    require(mixer.render(silence) && !mixer.playing(*voice), "EOF ends voice");
    stream->cancel();
    require(stream->buffer_bytes() == 0 && !stream->pump(error), "cancel releases buffer terminally");

    auto truncated = audio::PcmStream::open(path, 7, error);
    std::filesystem::resize_file(path, 0);
    require(!truncated->pump(error) && truncated->buffer_bytes() == 0,
        "truncated stream fails and releases buffer");
    // Restore a whole-frame source for invalid-start checks below.
    {
        std::ofstream file(path, std::ios::binary);
        file.write("abcd", 4);
    }

    const auto long_path = root / "long.pcm";
    {
        std::ofstream file(long_path, std::ios::binary);
        file.seekp(48000 * 120 * 4 - 1);
        file.put('\0');
    }
    auto long_stream = audio::PcmStream::open(long_path, 1024, error);
    require(long_stream && long_stream->pump(error) && long_stream->buffered_frames() == 1024,
        "two minute clip reads only bounded prefix");
    require(long_stream->buffer_bytes() <= 8192, "long clip resident buffer independent of file size");
    auto long_voice = mixer.play_stream(long_stream, {}, error);
    require(long_voice && mixer.stop(*long_voice), "stop streaming voice");
    require(long_stream->buffer_bytes() == 0 && long_stream->finished(), "stop cancels and releases buffer");
    require(!mixer.play_stream(long_stream, {}, error), "cancelled stream cannot restart");

    {
        std::ofstream file(root / "partial.pcm", std::ios::binary);
        file.put('x');
    }
    require(!audio::PcmStream::open(root / "partial.pcm", 7, error), "partial PCM frame refused");
    auto untouched = audio::PcmStream::open(path, 7, error);
    require(!mixer.play_stream(untouched, {1.F, 0.F, 1.F, true}, error), "stream looping refused");
    require(!mixer.play_stream(untouched, {1.F, 0.F, 2.F, false}, error), "stream pitch refused");
    require(mixer.play_stream(untouched, {}, error).has_value(), "invalid starts do not claim stream");
    untouched->cancel();
    require(mixer.render(silence) && mixer.active_voices() == 0, "external cancel stops on next render");
    std::filesystem::remove_all(root);
}
}

int main(int argc, char** argv)
{
    try {
        require(argc == 2, "test requires output directory");
        priority();
        streaming(argv[1]);
        std::cout << "audio priority and bounded streaming passed\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
