#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace lykoware::audio {

// GPT-owned, ADR 0083. Owns a raw stereo 48 kHz PCM16LE file and bounded ring.
// Lifetime: shared with at most one mixer voice. No copy/move. Single-threaded:
// serialize all calls with the mixer (Output::lock when attached to a device).
// Only pump performs I/O; never call it from the device callback. Errors return
// false/empty and fill error; allocation failure may throw. Cancel is terminal.
class PcmStream {
public:
    static std::shared_ptr<PcmStream> open(const std::filesystem::path& path,
                                          std::size_t capacity_frames, std::string& error);
    PcmStream(const PcmStream&) = delete;
    PcmStream& operator=(const PcmStream&) = delete;
    // Reads at most free ring space, never more than the explicit capacity.
    bool pump(std::string& error);
    void cancel();
    [[nodiscard]] std::size_t buffered_frames() const { return count_; }
    [[nodiscard]] std::size_t buffer_bytes() const { return ring_.capacity() * sizeof(float); }
    [[nodiscard]] bool finished() const { return remaining_ == 0 && count_ == 0; }

private:
    friend class Mixer;
    PcmStream() = default;
    bool take(std::array<float, 2>& frame);
    std::ifstream file_;
    std::vector<float> ring_;
    std::uint64_t remaining_ = 0;
    std::size_t head_ = 0;
    std::size_t count_ = 0;
    bool claimed_ = false;
};

}
