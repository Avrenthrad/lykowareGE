#pragma once

#include <cstdint>
#include <optional>
#include <string>

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
}
