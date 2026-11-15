#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>
#include <optional>
#include <string>

namespace lykoware::jobs { class Queue; }

namespace lykoware::streaming {
struct Position {
    double x = 0, y = 0, z = 0;
};
struct Region {
    std::int32_t x = 0, z = 0;
    bool operator==(const Region&) const = default;
};
struct Bounds {
    double min_x = 0, min_z = 0, max_x = 0, max_z = 0;
};
inline constexpr double region_size = 256;
// GPT-owned world partition (ADR 0089). Value inputs/outputs own their data;
// pure functions, safe across threads, no physics/ECS mutation. XZ metres,
// half-open bounds; altitude is validated but does not affect partitioning.
// region_at returns nullopt on nonfinite/out-of-domain positions. ID allocation
// errors propagate. Bounds and ID remain valid independent of any streamer.
[[nodiscard]] std::optional<Region> region_at(Position point);
[[nodiscard]] Bounds region_bounds(Region region);
[[nodiscard]] std::string region_id(Region region);

enum class LoadStatus { pending, loading, ready, failed, cancelled };
struct LoadResult {
    LoadStatus status = LoadStatus::pending;
    std::vector<std::byte> bytes;
    std::string error;
};
// Bounded synchronous read, no residency installation. Owns returned bytes.
// Failure is a failed result with no bytes; allocation failures propagate.
[[nodiscard]] LoadResult load_region_bytes(const std::filesystem::path& path, std::size_t byte_limit);

// GPT-owned asynchronous region request (ADR 0090). Owns result and job fence;
// the queue is borrowed only during start. One controlling thread; worker state
// is synchronised internally. Snapshots own their bytes and survive cancellation.
// cancel releases request-held bytes immediately and prevents late publication.
// Destructor cancels and waits for active I/O; never destroy from its worker.
// Queue refusal/I/O errors become failed results; allocation failures in start
// propagate. Workers do not mutate ECS, physics or residency. Explicit byte limit.
class RegionLoad {
public:
    [[nodiscard]] static std::unique_ptr<RegionLoad> start(jobs::Queue& queue, Region region,
        const std::filesystem::path& path, std::size_t byte_limit);
    ~RegionLoad();
    RegionLoad(const RegionLoad&) = delete;
    RegionLoad& operator=(const RegionLoad&) = delete;
    [[nodiscard]] Region region() const;
    [[nodiscard]] LoadResult snapshot() const;
    void cancel();
private:
    explicit RegionLoad(Region region);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
