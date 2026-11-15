#include <lykoware/streaming.hpp>

#include <cmath>
#include <limits>

namespace lykoware::streaming {
std::optional<Region> region_at(Position point)
{
    constexpr double lower = static_cast<double>(std::numeric_limits<std::int32_t>::min());
    constexpr double upper = -lower;
    if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)
        || point.x < lower * region_size || point.x >= upper * region_size
        || point.z < lower * region_size || point.z >= upper * region_size) {
        return std::nullopt;
    }
    const auto axis = [](double position) {
        double coordinate = std::floor(position / region_size);
        if (position < 0 && coordinate == 0) {
            coordinate = -1; // Division of a negative subnormal can round to -0.
        }
        return static_cast<std::int32_t>(coordinate);
    };
    return Region{axis(point.x), axis(point.z)};
}
Bounds region_bounds(Region region)
{
    const double x = static_cast<double>(region.x) * region_size;
    const double z = static_cast<double>(region.z) * region_size;
    return {x, z, x + region_size, z + region_size};
}
std::string region_id(Region region)
{
    return "r:" + std::to_string(region.x) + ":" + std::to_string(region.z);
}
}
