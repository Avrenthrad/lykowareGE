#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace lykoware::navigation {

struct Point {
    double x = 0;
    double z = 0;
    bool operator==(const Point&) const = default;
};

struct Solid {
    Point min;
    Point max;
};

struct Config {
    Point origin;
    double cell_size = 1;
    std::size_t columns = 0;
    std::size_t rows = 0;
};

enum class Status { reached, unreachable, invalid_input };
struct Route {
    Status status = Status::invalid_input;
    std::vector<Point> points;
};

// GPT-owned planar navigation (ADR 0076). Grid owns solids; Route owns its points
// and survives grid updates/destruction. One simulation thread; no concurrent
// updates/queries. Query never reads or writes physics/ECS. Coordinates are double
// XZ metres; callers choose the walking plane. No height or slope/headroom model.
// Failure: create returns nullopt and fills error; invalid solid updates return
// false without mutation; query reports invalid_input/unreachable with no points.
// Allocation failures propagate. Explicit dimensions are limited to 65,536 cells.
// Rasterization is conservative, including solid-edge contact and radius clearance
// at grid edges. Half-open grid bounds exclude the maximum outer boundary.
class Grid {
public:
    [[nodiscard]] static std::optional<Grid> create(const Config& config, std::string& error);
    [[nodiscard]] bool upsert(std::uint64_t id, Solid solid);
    [[nodiscard]] bool erase(std::uint64_t id);
    [[nodiscard]] Route query(Point from, Point to, double agent_radius) const;

private:
    explicit Grid(Config config) : config_(config) {}
    Config config_;
    std::map<std::uint64_t, Solid> solids_;
};

}
