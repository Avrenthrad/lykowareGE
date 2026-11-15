#include <lykoware/streaming.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}
}
int main()
{
    try {
        using namespace lykoware::streaming;
        const double inf = std::numeric_limits<double>::infinity();
        require(region_at({0, 0, 0}) == Region{0, 0}, "origin");
        require(region_at({-0.0, 3, -0.0}) == Region{0, 0}, "signed zero");
        require(region_at({std::nextafter(0.0, -inf), 0, 0}) == Region{-1, 0}, "negative subnormal");
        require(region_at({0, 0, std::nextafter(0.0, inf)}) == Region{0, 0}, "positive subnormal");
        for (int i : {-65, -33, -32, -1, 0, 1, 32, 33, 65}) {
            const double edge = static_cast<double>(i) * region_size;
            require(region_at({edge, 9999, edge}) == Region{i, i}, "exact edge, altitude ignored");
            require(region_at({std::nextafter(edge, -inf), 0, edge}) == Region{i - 1, i}, "immediately before edge");
            require(region_at({edge, 0, std::nextafter(edge, inf)}) == Region{i, i}, "immediately after edge");
            const auto bounds = region_bounds({i, -i});
            require(bounds.max_x - bounds.min_x == 256 && bounds.max_z - bounds.min_z == 256, "bounds size");
            require(region_at({bounds.min_x, 0, bounds.min_z}) == Region{i, -i}, "bounds minimum included");
            require(region_at({bounds.max_x, 0, bounds.max_z}) == Region{i + 1, -i + 1}, "bounds maximum excluded");
            for (int repeat = 0; repeat < 10; ++repeat) {
                require(region_id({i, -i}) == "r:" + std::to_string(i) + ":" + std::to_string(-i), "stable ID");
            }
        }
        require(region_at({16001.25, -100, -16001.25}) == Region{62, -63}, "past sixteen kilometres");
        const auto lowest = std::numeric_limits<std::int32_t>::min();
        const auto highest = std::numeric_limits<std::int32_t>::max();
        const auto low = region_bounds({lowest, lowest});
        const auto high = region_bounds({highest, highest});
        require(region_at({low.min_x, 0, low.min_z}) == Region{lowest, lowest}, "lowest region accepted");
        require(region_at({high.min_x, 0, high.min_z}) == Region{highest, highest}, "highest region accepted");
        require(!region_at({high.max_x, 0, 0}) && !region_at({0, 0, high.max_z}), "upper domain excluded");
        require(!region_at({std::nextafter(low.min_x, -inf), 0, 0}), "lower domain guarded");
        require(!region_at({inf, 0, 0}) && !region_at({0, -inf, 0})
                && !region_at({0, 0, std::numeric_limits<double>::quiet_NaN()}), "nonfinite refused");
        std::cout << "region partition passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
