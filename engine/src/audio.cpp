#include <lykoware/audio.hpp>
#include <lykoware/audio_stream.hpp>

#include <array>

#include "world_identity.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

namespace lykoware::audio {

namespace {

constexpr std::uint64_t one = std::uint64_t{1} << 32; // 1.0 in 32.32 fixed point

bool finite_in(float value, float low, float high)
{
    return std::isfinite(value) && value >= low && value <= high;
}

// Per-channel gains. A mono sound is panned with constant power (sqrt of the share, so the
// two squares add to gain^2); a stereo sound is balanced (the far channel turned down).
void channel_gains(const Sound& sound, float gain, float pan, float& left, float& right)
{
    if (sound.channels() == 1) {
        left = gain * std::sqrt((1.F - pan) * 0.5F);
        right = gain * std::sqrt((1.F + pan) * 0.5F);
    } else {
        left = pan <= 0.F ? gain : gain * (1.F - pan);
        right = pan >= 0.F ? gain : gain * (1.F + pan);
    }
}

// --- WAV reading ---------------------------------------------------------------------------

std::uint32_t read_u16(std::span<const std::byte> bytes, std::size_t at)
{
    return static_cast<std::uint32_t>(bytes[at]) | (static_cast<std::uint32_t>(bytes[at + 1]) << 8U);
}

std::uint32_t read_u32(std::span<const std::byte> bytes, std::size_t at)
{
    return read_u16(bytes, at) | (read_u16(bytes, at + 2) << 16U);
}

bool tag_is(std::span<const std::byte> bytes, std::size_t at, const char (&tag)[5])
{
    for (std::size_t i = 0; i < 4; ++i) {
        if (static_cast<char>(bytes[at + i]) != tag[i]) {
            return false;
        }
    }
    return true;
}

constexpr std::uint32_t format_pcm = 1;
constexpr std::uint32_t format_float = 3;
constexpr std::uint32_t format_extensible = 0xFFFE;

}

// --- Sound ---------------------------------------------------------------------------------

Sound::Sound(std::vector<float> samples, std::uint32_t channels, std::uint32_t sample_rate)
    : samples_(std::move(samples))
    , channels_(channels)
    , sample_rate_(sample_rate)
{
}

std::shared_ptr<const Sound> Sound::create(std::span<const float> samples, std::uint32_t channels,
                                           std::uint32_t sample_rate, std::string& error)
{
    if (channels != 1 && channels != 2) {
        error = "a sound has 1 or 2 channels";
        return nullptr;
    }
    if (sample_rate < min_sample_rate || sample_rate > max_sample_rate) {
        error = "the sample rate must be from " + std::to_string(min_sample_rate) + " to "
            + std::to_string(max_sample_rate) + " Hz";
        return nullptr;
    }
    if (samples.empty() || samples.size() % channels != 0 || samples.size() / channels > max_frames) {
        error = "a sound needs a whole number of frames, from 1 to " + std::to_string(max_frames);
        return nullptr;
    }
    for (const float sample : samples) {
        if (!finite_in(sample, -1.F, 1.F)) {
            error = "every sample must be a finite value from -1 to 1";
            return nullptr;
        }
    }
    return std::shared_ptr<const Sound>(new Sound({samples.begin(), samples.end()}, channels, sample_rate));
}

std::shared_ptr<const Sound> Sound::load_wav(std::span<const std::byte> file, std::string& error)
{
    if (file.size() < 12 || !tag_is(file, 0, "RIFF") || !tag_is(file, 8, "WAVE")) {
        error = "not a WAV file (no RIFF WAVE header)";
        return nullptr;
    }
    std::uint32_t format = 0;
    std::uint32_t channels = 0;
    std::uint32_t sample_rate = 0;
    std::uint32_t bits = 0;
    std::uint32_t block_align = 0;
    bool have_format = false;
    std::span<const std::byte> data;
    bool have_data = false;

    // Chunks: a 4-byte tag, a 4-byte size, the body, and a pad byte when the size is odd.
    std::size_t at = 12;
    while (at + 8 <= file.size()) {
        const std::uint32_t size = read_u32(file, at + 4);
        const std::size_t body = at + 8;
        if (size > file.size() - body) {
            error = "a WAV chunk runs past the end of the file";
            return nullptr;
        }
        if (tag_is(file, at, "fmt ")) {
            if (size < 16) {
                error = "the WAV format chunk is too short";
                return nullptr;
            }
            format = read_u16(file, body);
            channels = read_u16(file, body + 2);
            sample_rate = read_u32(file, body + 4);
            block_align = read_u16(file, body + 12);
            bits = read_u16(file, body + 14);
            if (format == format_extensible) {
                // The subformat GUID is {0000XXXX-0000-0010-8000-00AA00389B71}, where XXXX
                // (its first two bytes, little-endian) names the real format.
                if (size < 40) {
                    error = "the WAV extensible format chunk is too short";
                    return nullptr;
                }
                constexpr unsigned char guid_tail[14] = {0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80,
                                                         0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
                for (std::size_t i = 0; i < sizeof(guid_tail); ++i) {
                    if (static_cast<unsigned char>(file[body + 26 + i]) != guid_tail[i]) {
                        error = "unsupported WAV extensible subformat";
                        return nullptr;
                    }
                }
                format = read_u16(file, body + 24);
            }
            have_format = true;
        } else if (tag_is(file, at, "data")) {
            data = file.subspan(body, size);
            have_data = true;
        }
        at = body + size + (size % 2);
    }
    if (!have_format || !have_data) {
        error = "a WAV file needs a format chunk and a data chunk";
        return nullptr;
    }
    const bool pcm = format == format_pcm && (bits == 8 || bits == 16 || bits == 24 || bits == 32);
    const bool floating = format == format_float && bits == 32;
    if (!pcm && !floating) {
        error = "unsupported WAV encoding (only PCM 8/16/24/32-bit and 32-bit float are read)";
        return nullptr;
    }
    if (channels != 1 && channels != 2) {
        error = "a WAV file must be mono or stereo";
        return nullptr;
    }
    const std::uint32_t bytes_per_sample = bits / 8;
    if (block_align != bytes_per_sample * channels) {
        error = "the WAV block alignment does not match its channels and sample size";
        return nullptr;
    }
    if (data.size() % block_align != 0) {
        error = "the WAV data is not a whole number of frames";
        return nullptr;
    }

    std::vector<float> samples(data.size() / bytes_per_sample);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const std::size_t offset = i * bytes_per_sample;
        float value = 0.F;
        if (floating) {
            const std::uint32_t raw = read_u32(data, offset);
            std::memcpy(&value, &raw, sizeof(value));
            // Clamp finite floats that overshoot, a common export artefact; NaN and infinity
            // are left for create() to refuse.
            value = std::isfinite(value) ? std::clamp(value, -1.F, 1.F) : value;
        } else if (bits == 8) {
            // 8-bit WAV is unsigned, with silence at 128.
            value = (static_cast<float>(static_cast<std::uint32_t>(data[offset])) - 128.F) / 128.F;
        } else if (bits == 16) {
            value = static_cast<float>(static_cast<std::int16_t>(read_u16(data, offset))) / 32768.F;
        } else if (bits == 24) {
            std::uint32_t raw = static_cast<std::uint32_t>(data[offset]) | (static_cast<std::uint32_t>(data[offset + 1]) << 8U)
                | (static_cast<std::uint32_t>(data[offset + 2]) << 16U);
            raw = (raw & 0x800000U) != 0 ? raw | 0xFF000000U : raw; // sign-extend
            value = static_cast<float>(static_cast<std::int32_t>(raw)) / 8388608.F;
        } else {
            value = static_cast<float>(static_cast<double>(static_cast<std::int32_t>(read_u32(data, offset))) / 2147483648.0);
        }
        samples[i] = value;
    }
    return create(samples, channels, sample_rate, error);
}

// --- Mixer ---------------------------------------------------------------------------------

void Mixer::Ramp::set(float value)
{
    now = value;
    target = value;
    step = 0.F;
    frames = 0;
}

void Mixer::Ramp::retarget(float value)
{
    target = value;
    step = (target - now) / static_cast<float>(ramp_frames);
    frames = ramp_frames;
}

void Mixer::Ramp::advance()
{
    if (frames == 0) {
        return;
    }
    --frames;
    // The last frame lands on the target exactly, whatever rounding the steps gathered.
    now = frames == 0 ? target : now + step;
}

Mixer::Mixer()
    : owner_{detail::next_world_identity()}
{
    voices_.reserve(max_voices);
    master_.set(1.F);
}

Mixer::Voice* Mixer::find(VoiceId voice)
{
    return const_cast<Voice*>(std::as_const(*this).find(voice));
}

const Mixer::Voice* Mixer::find(VoiceId voice) const
{
    if (voice.owner != owner_ || voice.generation == 0 || voice.index >= voices_.size()) {
        return nullptr;
    }
    const Voice& slot = voices_[voice.index];
    return slot.active && slot.generation == voice.generation ? &slot : nullptr;
}

std::optional<VoiceId> Mixer::play(std::shared_ptr<const Sound> sound, const PlayParams& params, std::string& error)
{
    if (!sound) {
        error = "no sound to play";
        return std::nullopt;
    }
    if (!finite_in(params.gain, 0.F, 16.F) || !finite_in(params.pan, -1.F, 1.F)
        || !finite_in(params.pitch, 0.125F, 8.F)) {
        error = "gain must be 0..16, pan -1..1 and pitch 1/8..8";
        return std::nullopt;
    }
    // The lowest free slot. A slot whose generation would wrap is retired, so an old handle
    // never matches again.
    const auto reusable = [](const Voice& v) { return !v.active && v.generation != std::numeric_limits<std::uint32_t>::max(); };
    const auto slot = std::find_if(voices_.begin(), voices_.end(), reusable);
    std::uint32_t index = static_cast<std::uint32_t>(slot - voices_.begin());
    if (slot == voices_.end()) {
        if (voices_.size() >= max_voices) {
            error = "all " + std::to_string(max_voices) + " voices are playing";
            return std::nullopt;
        }
        voices_.emplace_back();
    }
    Voice& voice = voices_[index];
    voice.sound = std::move(sound);
    voice.stream.reset();
    voice.distance = -1.0;
    voice.position = 0;
    // Source frames per output frame in 32.32 fixed point, rounded once here so playback
    // positions are exact integers from then on.
    voice.step = static_cast<std::uint64_t>(std::llround(static_cast<double>(params.pitch)
                                                         * static_cast<double>(voice.sound->sample_rate())
                                                         / static_cast<double>(output_rate) * static_cast<double>(one)));
    voice.loop = params.loop;
    voice.gain = params.gain;
    voice.pan = params.pan;
    float left = 0.F;
    float right = 0.F;
    channel_gains(*voice.sound, voice.gain, voice.pan, left, right);
    voice.left.set(left);
    voice.right.set(right);
    ++voice.generation;
    voice.active = true;
    return VoiceId{index, voice.generation, owner_};
}

std::optional<VoiceId> Mixer::play_nearest(std::shared_ptr<const Sound> sound,
    const PlayParams& params, double distance, std::string& error)
{
    if (!sound || !std::isfinite(distance) || distance < 0.0
        || !finite_in(params.gain, 0.F, 16.F) || !finite_in(params.pan, -1.F, 1.F)
        || !finite_in(params.pitch, 0.125F, 8.F)) {
        error = "invalid prioritized voice parameters or distance";
        return std::nullopt;
    }
    auto result = play(sound, params, error);
    if (!result) {
        std::optional<VoiceId> victim;
        double farthest = distance;
        for (std::size_t i = 0; i < voices_.size(); ++i) {
            const Voice& candidate = voices_[i];
            if (candidate.active && !candidate.stream && candidate.distance > farthest
                && candidate.generation != std::numeric_limits<std::uint32_t>::max()) {
                farthest = candidate.distance;
                victim = VoiceId{static_cast<std::uint32_t>(i), candidate.generation, owner_};
            }
        }
        if (!victim) {
            return std::nullopt;
        }
        stop(*victim);
        result = play(std::move(sound), params, error);
    }
    if (result) {
        voices_[result->index].distance = distance;
        error.clear();
    }
    return result;
}

bool Mixer::set_distance(VoiceId voice, double distance)
{
    Voice* found = find(voice);
    if (!found || !std::isfinite(distance) || distance < 0.0) {
        return false;
    }
    found->distance = distance;
    return true;
}

std::optional<VoiceId> Mixer::play_stream(std::shared_ptr<PcmStream> stream,
    const PlayParams& params, std::string& error)
{
    if (!stream || stream->claimed_ || stream->finished() || params.loop || params.pitch != 1.F) {
        error = "stream must be live, unclaimed, nonlooping and pitch 1";
        return std::nullopt;
    }
    // Small immutable metadata carrier; render reads actual samples only from the ring.
    const std::array<float, 2> silence{};
    auto metadata = Sound::create(silence, 2, output_rate, error);
    auto result = play(std::move(metadata), params, error);
    if (result) {
        stream->claimed_ = true;
        voices_[result->index].stream = std::move(stream);
    }
    return result;
}

bool Mixer::stop(VoiceId voice)
{
    Voice* found = find(voice);
    if (found == nullptr) {
        return false;
    }
    found->active = false;
    found->sound.reset();
    if (found->stream) {
        found->stream->cancel();
        found->stream.reset();
    }
    return true;
}

bool Mixer::playing(VoiceId voice) const
{
    return find(voice) != nullptr;
}

void Mixer::retarget(Voice& voice)
{
    float left = 0.F;
    float right = 0.F;
    channel_gains(*voice.sound, voice.gain, voice.pan, left, right);
    voice.left.retarget(left);
    voice.right.retarget(right);
}

bool Mixer::set_gain(VoiceId voice, float gain)
{
    Voice* found = find(voice);
    if (found == nullptr || !finite_in(gain, 0.F, 16.F)) {
        return false;
    }
    found->gain = gain;
    retarget(*found);
    return true;
}

bool Mixer::set_pan(VoiceId voice, float pan)
{
    Voice* found = find(voice);
    if (found == nullptr || !finite_in(pan, -1.F, 1.F)) {
        return false;
    }
    found->pan = pan;
    retarget(*found);
    return true;
}

std::size_t Mixer::active_voices() const
{
    return static_cast<std::size_t>(std::count_if(voices_.begin(), voices_.end(), [](const Voice& v) { return v.active; }));
}

bool Mixer::set_master_gain(float gain)
{
    if (!finite_in(gain, 0.F, 16.F)) {
        return false;
    }
    master_.retarget(gain);
    return true;
}

bool Mixer::render(std::span<float> out)
{
    if (out.size() % output_channels != 0) {
        return false;
    }
    std::fill(out.begin(), out.end(), 0.F);
    const std::size_t frames = out.size() / output_channels;
    // Voices in slot order, each frame in turn: every output sample sums the voices in the same
    // order however the output is split into calls.
    for (Voice& voice : voices_) {
        if (!voice.active) {
            continue;
        }
        if (voice.stream) {
            for (std::size_t frame = 0; frame < frames; ++frame) {
                if (voice.stream->finished()) {
                    voice.active = false;
                    voice.stream.reset();
                    voice.sound.reset();
                    break;
                }
                std::array<float, 2> sample{};
                if (voice.stream->take(sample)) {
                    out[frame * 2] += sample[0] * voice.left.now;
                    out[frame * 2 + 1] += sample[1] * voice.right.now;
                }
                voice.left.advance();
                voice.right.advance();
            }
            continue;
        }
        const Sound& sound = *voice.sound;
        const std::span<const float> samples = sound.samples();
        const std::uint64_t length = static_cast<std::uint64_t>(sound.frames());
        const bool stereo = sound.channels() == 2;
        for (std::size_t frame = 0; frame < frames; ++frame) {
            std::uint64_t current = voice.position >> 32U;
            if (current >= length) {
                if (!voice.loop) {
                    // Nothing below uses `sound` again, so it may be the last reference.
                    voice.active = false;
                    voice.sound.reset();
                    break;
                }
                voice.position %= length << 32U;
                current = voice.position >> 32U;
            }
            // Linear interpolation towards the next frame; past the end that is silence, or the
            // first frame when looping. 24 bits of the fraction convert to float exactly.
            const float t = static_cast<float>((voice.position & 0xFFFFFFFFU) >> 8U) * (1.F / 16777216.F);
            const std::uint64_t next = current + 1 < length ? current + 1 : (voice.loop ? 0 : length);
            const auto sample_at = [&](std::uint64_t at, std::size_t channel) {
                return at < length ? samples[static_cast<std::size_t>(at) * sound.channels() + channel] : 0.F;
            };
            const float a_left = sample_at(current, 0);
            const float b_left = sample_at(next, 0);
            const float left = a_left + (b_left - a_left) * t;
            float right = left;
            if (stereo) {
                const float a_right = sample_at(current, 1);
                const float b_right = sample_at(next, 1);
                right = a_right + (b_right - a_right) * t;
            }
            out[frame * 2] += left * voice.left.now;
            out[frame * 2 + 1] += right * voice.right.now;
            voice.left.advance();
            voice.right.advance();
            voice.position += voice.step;
        }
    }
    for (std::size_t frame = 0; frame < frames; ++frame) {
        out[frame * 2] = std::clamp(out[frame * 2] * master_.now, -1.F, 1.F);
        out[frame * 2 + 1] = std::clamp(out[frame * 2 + 1] * master_.now, -1.F, 1.F);
        master_.advance();
    }
    frames_rendered_ += frames;
    return true;
}

}
