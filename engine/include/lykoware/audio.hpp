#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Lykoware's audio mixer (ADR 0032). Lykoware's own code: sounds, voices and a software mixer
// that renders interleaved stereo float samples. It needs no audio device, so every result can
// be tested exactly; sending the samples to a device is a separate layer.
//
// Determinism: the same calls give bit-identical output on every supported compiler and CPU,
// however the output is split into render() calls. Only +, -, *, / and sqrt are used on
// samples (IEEE 754 rounds them identically everywhere), and playback positions are integers.

namespace lykoware::audio {

// The mixer's output: frames per second and interleaved channels (left, right).
inline constexpr std::uint32_t output_rate = 48000;
inline constexpr std::uint32_t output_channels = 2;
// Voices that can play at once.
inline constexpr std::uint32_t max_voices = 64;
// Sample rates a Sound may have.
inline constexpr std::uint32_t min_sample_rate = 8000;
inline constexpr std::uint32_t max_sample_rate = 192000;
// Longest Sound, in frames: an hour at the highest rate.
inline constexpr std::size_t max_frames = std::size_t{max_sample_rate} * 3600;
// Frames over which gain and pan changes are ramped, so they do not click (5 ms).
inline constexpr std::uint32_t ramp_frames = 240;

// Decoded audio: float samples in [-1, 1], mono or interleaved stereo, at its own sample rate.
// Immutable once made and shared by every voice playing it.
class Sound {
public:
    // From samples. Fails (nullptr, error filled) for 0 or more than 2 channels, a rate outside
    // min_sample_rate..max_sample_rate, no frames or more than max_frames, a sample count that
    // is not a whole number of frames, or a sample that is not finite or outside [-1, 1].
    [[nodiscard]] static std::shared_ptr<const Sound> create(std::span<const float> samples, std::uint32_t channels,
                                                             std::uint32_t sample_rate, std::string& error);
    // From the bytes of a WAV file: PCM 8, 16, 24 or 32 bit, or 32-bit float, mono or stereo,
    // including WAVE_FORMAT_EXTENSIBLE with a PCM or float subformat. Fails for anything else,
    // for malformed or truncated files, and for the limits of create().
    [[nodiscard]] static std::shared_ptr<const Sound> load_wav(std::span<const std::byte> file, std::string& error);

    [[nodiscard]] std::uint32_t channels() const { return channels_; }
    [[nodiscard]] std::uint32_t sample_rate() const { return sample_rate_; }
    [[nodiscard]] std::size_t frames() const { return samples_.size() / channels_; }
    [[nodiscard]] std::span<const float> samples() const { return samples_; }

private:
    Sound(std::vector<float> samples, std::uint32_t channels, std::uint32_t sample_rate);

    std::vector<float> samples_;
    std::uint32_t channels_;
    std::uint32_t sample_rate_;
};

// Handle to a playing voice, with the same rules as ECS and physics handles: a finished or
// stopped voice's handle stays invalid, even when its slot plays something else. The default
// handle is never valid. A new voice takes the lowest free slot, so which slot it gets (and so
// the order voices are summed in) does not depend on how the output was split into calls.
struct VoiceId {
    std::uint32_t index = 0;
    std::uint32_t generation = 0;

    friend bool operator==(const VoiceId&, const VoiceId&) = default;
};

struct PlayParams {
    // Linear gain, 0..16.
    float gain = 1.F;
    // -1 is fully left, 0 centred, 1 fully right. Constant power: left^2 + right^2 = 1.
    float pan = 0.F;
    // Playback speed (and pitch) multiplier, 1/8..8.
    float pitch = 1.F;
    // Start again from the beginning at the end, until stopped.
    bool loop = false;
};

// Mixes voices into interleaved stereo float output at output_rate.
//
// Threading: one thread at a time; an output layer that renders on an audio thread must lock.
// Failure: functions taking a VoiceId return false for a stale or invalid handle. Nothing
// throws for misuse; std::bad_alloc can propagate.
class Mixer {
public:
    Mixer();

    // Starts a voice. Fails (nullopt, error filled) for a null sound, parameters out of range or
    // not finite, or when max_voices voices are already playing.
    [[nodiscard]] std::optional<VoiceId> play(std::shared_ptr<const Sound> sound, const PlayParams& params,
                                              std::string& error);
    bool stop(VoiceId voice);
    [[nodiscard]] bool playing(VoiceId voice) const;
    // Changes ramp over ramp_frames. False for an invalid handle or an out-of-range value.
    bool set_gain(VoiceId voice, float gain);
    bool set_pan(VoiceId voice, float pan);
    [[nodiscard]] std::size_t active_voices() const;

    // Gain applied to the sum of all voices, 0..16 (default 1), ramped over ramp_frames like a
    // voice's gain. Output is then clamped to [-1, 1]. False for an out-of-range value.
    bool set_master_gain(float gain);

    // Writes the next out.size() / 2 frames of interleaved stereo into `out` (overwriting it).
    // An odd size renders nothing and returns false. Voices that reach their end stop.
    bool render(std::span<float> out);
    [[nodiscard]] std::uint64_t frames_rendered() const { return frames_rendered_; }

private:
    // A gain that moves to a new target over ramp_frames, landing on it exactly.
    struct Ramp {
        float now = 0.F;
        float target = 0.F;
        float step = 0.F;
        std::uint32_t frames = 0; // frames still to go

        // Jumps straight to a value.
        void set(float value);
        // Starts moving from the current value to a new one.
        void retarget(float value);
        // Moves one frame on.
        void advance();
    };

    struct Voice {
        std::shared_ptr<const Sound> sound;
        // Position in the sound's frames, 32.32 fixed point, and the step per output frame.
        std::uint64_t position = 0;
        std::uint64_t step = 0;
        bool loop = false;
        // Per-channel gains, from gain and pan.
        Ramp left;
        Ramp right;
        float gain = 1.F;
        float pan = 0.F;
        std::uint32_t generation = 0;
        bool active = false;
    };

    Voice* find(VoiceId voice);
    [[nodiscard]] const Voice* find(VoiceId voice) const;
    static void retarget(Voice& voice);

    std::vector<Voice> voices_;
    Ramp master_;
    std::uint64_t frames_rendered_ = 0;
};

}
