// audio.output: the mixer played through SDL3's audio thread (ADR 0033). Uses the dummy and disk
// drivers, so it needs no sound card. The disk driver captures the exact bytes the device would
// play, which must equal what the mixer renders on its own.

#include <lykoware/audio.hpp>
#include <lykoware/audio_output.hpp>
#include <lykoware/window.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
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

std::unique_ptr<audio::Output> open_output(const audio::OutputConfig& config)
{
    std::string error;
    auto output = audio::Output::open(config, error);
    if (!output) {
        std::cerr << "FATAL: audio output: " << error << '\n';
        std::exit(EXIT_FAILURE);
    }
    return output;
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

// Waits up to five seconds for a condition the audio thread brings about.
template <typename Condition>
bool wait_for(Condition condition)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return condition();
}

std::uint64_t frames_rendered(audio::Output& output)
{
    return output.lock()->frames_rendered();
}

audio::OutputConfig dummy_config()
{
    audio::OutputConfig config;
    config.driver = audio::OutputDriver::Dummy;
    config.real_time = false;
    return config;
}

void test_dummy_device()
{
    const auto output = open_output(dummy_config());
    expect(output->driver_name() == "dummy", "the dummy driver is selected");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    expect(frames_rendered(*output) == 0, "a new output starts paused");

    audio::VoiceId loop{};
    audio::VoiceId once{};
    {
        auto mixer = output->lock();
        std::string error;
        loop = mixer->play(make(std::vector<float>(64, 0.25F)), {1.F, 0.F, 1.F, true}, error).value_or(audio::VoiceId{});
        once = mixer->play(make(std::vector<float>(100, 0.5F)), {}, error).value_or(audio::VoiceId{});
    }
    expect(output->resume(), "the device resumes");
    expect(wait_for([&] { return frames_rendered(*output) >= audio::output_rate; }),
           "the audio thread pulls a second of audio from the mixer");
    {
        auto mixer = output->lock();
        expect(mixer->playing(loop), "a looping voice keeps playing on the audio thread");
        expect(!mixer->playing(once), "a short voice ends on the audio thread");
    }

    expect(output->pause(), "the device pauses");
    const std::uint64_t paused_at = frames_rendered(*output);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    // The audio thread may finish a callback that was already running when pause() returned.
    const std::uint64_t after_pause = frames_rendered(*output);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    expect(frames_rendered(*output) == after_pause && after_pause - paused_at <= 8192,
           "a paused device stops pulling audio");
}

// Game code changes voices while the audio thread renders: the lock keeps them apart. Under the
// sanitizer build this also checks for memory errors on both threads.
void test_concurrent_use()
{
    const auto output = open_output(dummy_config());
    expect(output->resume(), "the device resumes");
    const auto sound = make(std::vector<float>(480, 0.1F));
    std::string error;
    for (int i = 0; i < 2000; ++i) {
        auto mixer = output->lock();
        if (const auto voice = mixer->play(sound, {0.5F, 0.F, 1.F, (i % 3) == 0}, error)) {
            mixer->set_pan(*voice, (i % 2) == 0 ? -0.5F : 0.5F);
            if ((i % 5) == 0) {
                mixer->stop(*voice);
            }
        }
    }
    expect(wait_for([&] { return frames_rendered(*output) >= audio::output_rate; }),
           "the audio thread keeps rendering while voices change");
}

void test_session_rules()
{
    std::string error;
    audio::OutputConfig no_file;
    no_file.driver = audio::OutputDriver::Disk;
    expect(!audio::Output::open(no_file, error) && !error.empty(), "the disk driver without a file is refused");
    audio::OutputConfig stray_file = dummy_config();
    stray_file.disk_file = "x.raw";
    expect(!audio::Output::open(stray_file, error), "a file for a driver that writes none is refused");

    const auto first = open_output(dummy_config());
    const auto second = open_output(dummy_config());
    expect(first && second, "two outputs can share the dummy driver");
    audio::OutputConfig disk;
    disk.driver = audio::OutputDriver::Disk;
    disk.disk_file = "unused.raw";
    expect(!audio::Output::open(disk, error) && !error.empty(), "a different driver is refused while one is open");
}

// Closing the last window must not stop audio: the two SDL sessions are independent.
void test_independent_of_windows()
{
    const auto output = open_output(dummy_config());
    expect(output->resume(), "the device resumes");
    {
        lykoware::WindowConfig config;
        config.driver = lykoware::VideoDriver::Dummy;
        config.visible = false;
        std::string error;
        auto window = lykoware::Window::open(config, error);
        expect(window != nullptr, "a dummy window opens beside the audio output");
    }
    const std::uint64_t before = frames_rendered(*output);
    expect(wait_for([&] { return frames_rendered(*output) >= before + audio::output_rate; }),
           "audio keeps playing after the last window closes");
}

// The bytes reaching the device are exactly the mixer's output: no conversion, no extra gain.
void test_disk_capture_is_exact(const std::filesystem::path& directory)
{
    std::filesystem::create_directories(directory);
    const std::filesystem::path file = directory / "capture.raw";
    std::filesystem::remove(file);

    const auto tone = make({0.3F, -0.6F, 0.9F, -0.2F, 0.1F, 0.7F}, 1, 22050);
    const auto stereo = make({0.5F, -0.5F, 0.25F, 0.75F, -0.125F, 0.375F}, 2, 44100);
    const audio::PlayParams tone_params{0.8F, -0.3F, 1.3F, true};
    const audio::PlayParams stereo_params{0.6F, 0.4F, 0.9F, true};

    std::uint64_t rendered = 0;
    {
        audio::OutputConfig config;
        config.driver = audio::OutputDriver::Disk;
        config.disk_file = file.string();
        config.real_time = false;
        const auto output = open_output(config);
        expect(output->driver_name() == "disk", "the disk driver is selected");
        const audio::Output::DeviceFormat format = output->device_format();
        expect(format.float32 && format.channels == 2 && format.sample_rate == audio::output_rate,
               "the device receives 32-bit float stereo at 48 kHz, so SDL converts nothing");
        {
            auto mixer = output->lock();
            std::string error;
            expect(mixer->play(tone, tone_params, error).has_value() && mixer->play(stereo, stereo_params, error).has_value(),
                   "voices start before the device resumes");
        }
        expect(output->resume(), "the device resumes");
        expect(wait_for([&] { return frames_rendered(*output) >= 3 * audio::output_rate; }),
               "the disk device pulls three seconds of audio");
        expect(output->pause(), "the device pauses");
        rendered = frames_rendered(*output);
    } // closing the output flushes and closes the file

    std::ifstream in(file, std::ios::binary);
    const std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<float> captured(bytes.size() / sizeof(float));
    std::memcpy(captured.data(), bytes.data(), captured.size() * sizeof(float));

    // While paused (before resume() and after pause()), the device writes silence. The mixer's
    // output, which has no silent frame with these voices, lies between.
    std::size_t start = 0;
    while (start < captured.size() && captured[start] == 0.F) {
        ++start;
    }
    start -= start % 2;
    std::size_t end = captured.size() - captured.size() % 2;
    while (end > start && captured[end - 1] == 0.F && captured[end - 2] == 0.F) {
        end -= 2;
    }
    const std::size_t played = (end - start) / 2;

    audio::Mixer reference;
    std::string error;
    expect(reference.play(tone, tone_params, error).has_value() && reference.play(stereo, stereo_params, error).has_value(),
           "the reference mixer starts the same voices");
    std::vector<float> expected(static_cast<std::size_t>(rendered) * 2);
    reference.render(expected);

    bool exact = played >= audio::output_rate && played <= rendered;
    for (std::size_t i = 0; exact && i < played * 2; ++i) {
        exact = captured[start + i] == expected[i];
    }
    expect(exact, "the device bytes equal the mixer's own output, bit for bit");
    std::cout << "disk capture: " << played << " frames played of " << rendered << " rendered\n";
}

}

int main(int argc, char** argv)
{
    const std::filesystem::path directory = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("audio-output");
    test_dummy_device();
    test_concurrent_use();
    test_session_rules();
    test_independent_of_windows();
    test_disk_capture_is_exact(directory);
    if (g_failures != 0) {
        std::cerr << g_failures << " audio output assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "audio output passed\n";
    return EXIT_SUCCESS;
}
