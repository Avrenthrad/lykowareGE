#pragma once

#include <lykoware/audio.hpp>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

// Lykoware's audio output (ADR 0033): plays a Mixer on an audio device through SDL3. SDL pulls
// audio on its own audio thread, which renders the mixer under the output's lock. No SDL type
// appears in this header.

namespace lykoware::audio {

enum class OutputDriver {
    // The platform's default device: WASAPI on Windows; PipeWire, PulseAudio or ALSA on Linux.
    Platform,
    // No device: the audio is consumed and thrown away. Tests and CI use this.
    Dummy,
    // No device: the exact bytes the device would play (32-bit float, stereo, 48 kHz,
    // native byte order) are written to OutputConfig::disk_file. Tests use this.
    Disk
};

struct OutputConfig {
    OutputDriver driver = OutputDriver::Platform;
    // Disk only: the file written. Required for Disk.
    std::string disk_file;
    // Dummy and Disk only: false consumes audio as fast as the mixer renders it, instead of at
    // 48,000 frames a second. Tests use this to run quickly.
    bool real_time = true;
};

// Exclusive access to an output's mixer. The audio thread waits while one is held, so hold it
// only for a few mixer calls, never across a frame.
class MixerLock {
public:
    Mixer* operator->() const { return mixer_; }
    Mixer& operator*() const { return *mixer_; }

private:
    friend class Output;
    MixerLock(std::mutex& mutex, Mixer& mixer)
        : lock_(mutex)
        , mixer_(&mixer)
    {
    }

    std::unique_lock<std::mutex> lock_;
    Mixer* mixer_;
};

// One open audio device playing one Mixer.
//
// Ownership: open() returns the sole owner, which owns the Mixer. The destructor stops the
// device and waits for the audio thread to finish with the mixer.
//
// Threading: make every Mixer call through lock(). SDL's audio thread renders the mixer while
// holding the same lock. Every output in a process shares one SDL audio session and its driver;
// it is independent of the window session, so closing every window does not stop audio.
//
// Lifetime: the device starts paused, so voices can be set up before anything plays. resume()
// starts it. A Sound freed when its last voice ends is freed on the audio thread.
//
// Failure: open() returns nullptr and fills error when SDL's audio cannot start, the driver
// differs from an output already open, or Disk has no file. Nothing throws for a device refusal;
// std::bad_alloc can propagate.
class Output {
public:
    [[nodiscard]] static std::unique_ptr<Output> open(const OutputConfig& config, std::string& error);

    ~Output();
    Output(const Output&) = delete;
    Output& operator=(const Output&) = delete;
    Output(Output&&) = delete;
    Output& operator=(Output&&) = delete;

    [[nodiscard]] MixerLock lock() { return MixerLock(mutex_, mixer_); }

    // Starts or stops pulling audio. False if SDL refuses.
    bool resume();
    bool pause();

    // SDL's name for the driver in use, e.g. "wasapi", "pipewire", "dummy" or "disk".
    [[nodiscard]] std::string driver_name() const;

    // The format the device itself runs at. Lykoware always sends 32-bit float stereo at
    // output_rate; when the device differs (a 44.1 kHz sound card, say), SDL converts.
    struct DeviceFormat {
        std::uint32_t sample_rate = 0;
        std::uint32_t channels = 0;
        bool float32 = false;
    };
    [[nodiscard]] DeviceFormat device_format() const;

private:
    struct Device;

    Output();

    std::mutex mutex_;
    Mixer mixer_;
    std::unique_ptr<Device> device_;
};

}
