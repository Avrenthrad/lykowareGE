#include <lykoware/audio_stream.hpp>

#include <algorithm>

namespace lykoware::audio {

std::shared_ptr<PcmStream> PcmStream::open(const std::filesystem::path& path,
                                           std::size_t capacity_frames, std::string& error)
{
    error.clear();
    if (capacity_frames == 0 || capacity_frames > 48000) {
        error = "PCM stream capacity must be 1..48000 frames";
        return {};
    }
    auto stream = std::shared_ptr<PcmStream>(new PcmStream);
    stream->file_.open(path, std::ios::binary | std::ios::ate);
    const auto size = stream->file_.tellg();
    if (!stream->file_ || size <= 0 || size % 4 != 0) {
        error = "PCM stream needs a nonempty stereo PCM16LE file (whole 4-byte frames)";
        return {};
    }
    stream->remaining_ = static_cast<std::uint64_t>(size) / 4;
    stream->file_.seekg(0);
    stream->ring_.resize(capacity_frames * 2);
    return stream;
}

bool PcmStream::pump(std::string& error)
{
    error.clear();
    if (ring_.empty()) {
        error = "PCM stream is cancelled";
        return false;
    }
    const auto capacity = ring_.size() / 2;
    const auto frames = std::min<std::uint64_t>(remaining_, capacity - count_);
    for (std::uint64_t i = 0; i < frames; ++i) {
        std::array<unsigned char, 4> bytes{};
        if (!file_.read(reinterpret_cast<char*>(bytes.data()), 4)) {
            error = "PCM stream truncated or read failed";
            cancel();
            return false;
        }
        const auto at = (head_ + count_) % capacity;
        for (std::size_t channel = 0; channel < 2; ++channel) {
            const int value = static_cast<int>(bytes[channel * 2])
                            + static_cast<int>(bytes[channel * 2 + 1]) * 256;
            ring_[at * 2 + channel] = static_cast<float>(value >= 32768 ? value - 65536 : value) / 32768.F;
        }
        ++count_;
        --remaining_;
    }
    if (remaining_ == 0 && file_.is_open()) {
        file_.close();
    }
    return true;
}

bool PcmStream::take(std::array<float, 2>& frame)
{
    if (count_ == 0) {
        return false;
    }
    frame = {ring_[head_ * 2], ring_[head_ * 2 + 1]};
    head_ = (head_ + 1) % (ring_.size() / 2);
    --count_;
    return true;
}

void PcmStream::cancel()
{
    if (file_.is_open()) {
        file_.close();
    }
    std::vector<float>().swap(ring_);
    remaining_ = 0;
    head_ = 0;
    count_ = 0;
}

}
