#include <lykoware/navigation.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace lykoware::navigation {
namespace {
bool finite(Point point)
{
    return std::isfinite(point.x) && std::isfinite(point.z);
}
}

std::optional<Grid> Grid::create(const Config& config, std::string& error)
{
    constexpr std::size_t max_cells = 65'536;
    if (!finite(config.origin) || !std::isfinite(config.cell_size) || config.cell_size <= 0
        || config.columns == 0 || config.rows == 0 || config.columns > max_cells
        || config.rows > max_cells / config.columns) {
        error = "invalid navigation dimensions (maximum 65536 cells)";
        return std::nullopt;
    }
    const double end_x = config.origin.x + static_cast<double>(config.columns) * config.cell_size;
    const double end_z = config.origin.z + static_cast<double>(config.rows) * config.cell_size;
    // Every cell must remain representable, even far from the origin.
    if (!std::isfinite(end_x) || !std::isfinite(end_z)
        || config.origin.x + config.cell_size <= config.origin.x
        || config.origin.z + config.cell_size <= config.origin.z
        || end_x - config.cell_size >= end_x || end_z - config.cell_size >= end_z) {
        error = "navigation bounds collapse or overflow";
        return std::nullopt;
    }
    error.clear();
    return Grid(config);
}

bool Grid::upsert(std::uint64_t id, Solid solid)
{
    if (id == 0 || !finite(solid.min) || !finite(solid.max)
        || solid.min.x >= solid.max.x || solid.min.z >= solid.max.z) {
        return false;
    }
    solids_.insert_or_assign(id, solid);
    return true;
}

bool Grid::erase(std::uint64_t id)
{
    return solids_.erase(id) != 0;
}

Route Grid::query(Point from, Point to, double agent_radius) const
{
    const auto& c = config_;
    const Point end{c.origin.x + static_cast<double>(c.columns) * c.cell_size,
                    c.origin.z + static_cast<double>(c.rows) * c.cell_size};
    const auto inside = [&](Point p) {
        return finite(p) && p.x >= c.origin.x && p.z >= c.origin.z && p.x < end.x && p.z < end.z;
    };
    if (!inside(from) || !inside(to) || !std::isfinite(agent_radius) || agent_radius < 0) {
        return {};
    }
    const auto cell = [&](Point p) {
        const auto x = static_cast<std::size_t>((p.x - c.origin.x) / c.cell_size);
        const auto z = static_cast<std::size_t>((p.z - c.origin.z) / c.cell_size);
        return z * c.columns + x;
    };
    const std::size_t count = c.columns * c.rows;
    const auto start = cell(from);
    const auto goal = cell(to);
    // Rounding at the upper edge must not form an out-of-range cell index.
    if (start >= count || goal >= count
        || (from.x - c.origin.x) / c.cell_size >= static_cast<double>(c.columns)
        || (to.x - c.origin.x) / c.cell_size >= static_cast<double>(c.columns)) {
        return {};
    }
    std::vector<bool> blocked(count, false);
    for (std::size_t i = 0; i < count; ++i) {
        const Point low{c.origin.x + static_cast<double>(i % c.columns) * c.cell_size,
                        c.origin.z + static_cast<double>(i / c.columns) * c.cell_size};
        const Point high{low.x + c.cell_size, low.z + c.cell_size};
        blocked[i] = low.x - agent_radius < c.origin.x || low.z - agent_radius < c.origin.z
            || high.x + agent_radius > end.x || high.z + agent_radius > end.z;
        for (const auto& entry : solids_) {
            const auto& solid = entry.second;
            if (low.x <= solid.max.x + agent_radius && high.x >= solid.min.x - agent_radius
                && low.z <= solid.max.z + agent_radius && high.z >= solid.min.z - agent_radius) {
                blocked[i] = true;
                break;
            }
        }
    }
    if (blocked[start] || blocked[goal]) {
        return {Status::unreachable, {}};
    }
    constexpr auto unseen = std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> parent(count, unseen);
    std::vector<std::size_t> queue;
    queue.reserve(count);
    parent[start] = start;
    queue.push_back(start);
    for (std::size_t head = 0; head < queue.size() && parent[goal] == unseen; ++head) {
        const auto current = queue[head];
        const auto x = current % c.columns;
        const auto z = current / c.columns;
        const auto visit = [&](std::size_t next) {
            if (!blocked[next] && parent[next] == unseen) {
                parent[next] = current;
                queue.push_back(next);
            }
        };
        if (x + 1 < c.columns) { visit(current + 1); }
        if (z + 1 < c.rows) { visit(current + c.columns); }
        if (x > 0) { visit(current - 1); }
        if (z > 0) { visit(current - c.columns); }
    }
    if (parent[goal] == unseen) {
        return {Status::unreachable, {}};
    }
    std::vector<std::size_t> cells;
    for (auto i = goal;; i = parent[i]) {
        cells.push_back(i);
        if (i == start) { break; }
    }
    std::reverse(cells.begin(), cells.end());
    Route result{Status::reached, {from}};
    for (const auto i : cells) {
        const Point centre{c.origin.x + (static_cast<double>(i % c.columns) + 0.5) * c.cell_size,
                           c.origin.z + (static_cast<double>(i / c.columns) + 0.5) * c.cell_size};
        if (result.points.back() != centre) { result.points.push_back(centre); }
    }
    if (result.points.back() != to) { result.points.push_back(to); }
    return result;
}

}
