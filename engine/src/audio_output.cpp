#include <lykoware/audio_output.hpp>

#include <SDL3/SDL.h>

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace lykoware::audio {

namespace {

// The process-wide SDL audio session. Every output shares it and its driver. It starts and stops
// only SDL's audio subsystem, so it never ends the window session (or the reverse).
struct Session {
    int holders = 0;
    OutputDriver driver = OutputDriver::Platform;
};

Session& session()
{
    static Session value;
    return value;
}

const char* forced_driver_name(OutputDriver driver) noexcept
{
    switch (driver) {
    case OutputDriver::Dummy: return "dummy";
    case OutputDriver::Disk: return "disk";
    case OutputDriver::Platform: return nullptr;
    }
    return nullptr;
}

std::string sdl_failure(std::string_view what)
{
    const char* detail = SDL_GetError();
    return std::string{what} + " failed: " + (detail != nullptr && *detail != '\0' ? detail : "no detail from SDL");
}

void quit_audio()
{
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    // SDL_Quit also frees SDL's global state, but only once nothing else (a window) uses SDL.
    if (SDL_WasInit(0) == 0) {
        SDL_Quit();
    }
}

bool session_acquire(OutputDriver driver, std::string& error)
{
    Session& current = session();
    if (current.holders > 0) {
        if (current.driver != driver) {
            error = "an audio output is already open with a different driver";
            return false;
        }
        ++current.holders;
        return true;
    }
    // The driver hint is read when the audio subsystem starts. Platform resets it (an
    // SDL_AUDIO_DRIVER environment variable still applies), so an earlier test driver does not stick.
    const char* const forced = forced_driver_name(driver);
    if (!(forced != nullptr ? SDL_SetHint(SDL_HINT_AUDIO_DRIVER, forced) : SDL_ResetHint(SDL_HINT_AUDIO_DRIVER))) {
        error = "could not select the audio driver";
        return false;
    }
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        error = sdl_failure("SDL_InitSubSystem(SDL_INIT_AUDIO)");
        return false;
    }
    if (forced != nullptr) {
        const char* name = SDL_GetCurrentAudioDriver();
        if (name == nullptr || std::string_view{name} != forced) {
            quit_audio();
            error = std::string{forced} + " audio driver was not selected";
            return false;
        }
    }
    current.driver = driver;
    current.holders = 1;
    return true;
}

void session_release()
{
    Session& current = session();
    if (current.holders <= 0) {
        return;
    }
    if (--current.holders == 0) {
        quit_audio();
    }
}

// Frames rendered per mixer call on the audio thread; a device asking for more gets several.
constexpr std::size_t chunk_frames = 1024;

}

struct Output::Device {
    Output& owner;
    SDL_AudioStream* stream = nullptr;
    // Rendered on the audio thread only. Fixed size, so the audio thread never allocates.
    std::array<float, chunk_frames * output_channels> buffer{};

    explicit Device(Output& output)
        : owner(output)
    {
    }

    // SDL calls this on its audio thread, holding the stream's lock, when the device needs
    // `additional` more bytes.
    static void SDLCALL feed(void* userdata, SDL_AudioStream* sdl_stream, int additional, int /*total*/)
    {
        Device& device = *static_cast<Device*>(userdata);
        constexpr std::size_t frame_bytes = output_channels * sizeof(float);
        std::size_t frames = additional > 0 ? (static_cast<std::size_t>(additional) + frame_bytes - 1) / frame_bytes : 0;
        while (frames > 0) {
            const std::size_t now = frames < chunk_frames ? frames : chunk_frames;
            const std::span<float> out(device.buffer.data(), now * output_channels);
            {
                const std::lock_guard<std::mutex> lock(device.owner.mutex_);
                device.owner.mixer_.render(out);
            }
            if (!SDL_PutAudioStreamData(sdl_stream, out.data(), static_cast<int>(out.size_bytes()))) {
                return; // SDL is out of memory; the device plays what it has
            }
            frames -= now;
        }
    }
};

Output::Output() = default;

std::unique_ptr<Output> Output::open(const OutputConfig& config, std::string& error)
{
    if (config.driver == OutputDriver::Disk && config.disk_file.empty()) {
        error = "the disk audio driver needs a file";
        return nullptr;
    }
    if (config.driver != OutputDriver::Disk && !config.disk_file.empty()) {
        error = "only the disk audio driver writes a file";
        return nullptr;
    }
    if (!session_acquire(config.driver, error)) {
        return nullptr;
    }
    // These are read when the device opens. "0" makes the device thread run without waiting.
    if (config.driver == OutputDriver::Disk) {
        SDL_SetHint(SDL_HINT_AUDIO_DISK_OUTPUT_FILE, config.disk_file.c_str());
        SDL_SetHint(SDL_HINT_AUDIO_DISK_TIMESCALE, config.real_time ? "1" : "0");
    } else if (config.driver == OutputDriver::Dummy) {
        SDL_SetHint(SDL_HINT_AUDIO_DUMMY_TIMESCALE, config.real_time ? "1" : "0");
    }

    std::unique_ptr<Output> output(new Output());
    output->device_ = std::make_unique<Device>(*output);
    const SDL_AudioSpec spec{SDL_AUDIO_F32, static_cast<int>(output_channels), static_cast<int>(output_rate)};
    output->device_->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &Device::feed,
                                                        output->device_.get());
    if (output->device_->stream == nullptr) {
        error = sdl_failure("SDL_OpenAudioDeviceStream");
        output->device_.reset();
        session_release();
        return nullptr;
    }
    error.clear();
    return output;
}

Output::~Output()
{
    if (device_ != nullptr && device_->stream != nullptr) {
        // Closes the device and waits for any callback in progress, so the audio thread is done
        // with the mixer before it is destroyed. The output's lock must not be held here.
        SDL_DestroyAudioStream(device_->stream);
        device_.reset();
        session_release();
    }
}

bool Output::resume()
{
    return SDL_ResumeAudioStreamDevice(device_->stream);
}

bool Output::pause()
{
    return SDL_PauseAudioStreamDevice(device_->stream);
}

std::string Output::driver_name() const
{
    const char* name = SDL_GetCurrentAudioDriver();
    return name != nullptr ? name : "";
}

Output::DeviceFormat Output::device_format() const
{
    SDL_AudioSpec spec{};
    if (!SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(device_->stream), &spec, nullptr)) {
        return {};
    }
    return {static_cast<std::uint32_t>(spec.freq), static_cast<std::uint32_t>(spec.channels), spec.format == SDL_AUDIO_F32};
}

}
