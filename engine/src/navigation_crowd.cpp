#include <lykoware/navigation_crowd.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>

namespace lykoware::navigation {
namespace {
std::uint64_t priority(std::uint64_t value)
{
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}
// Distance between two simultaneous linear motions, including stationary agents.
bool separated(Point a, Point end_a, Point b, Point end_b, double clearance)
{
    const double x = a.x - b.x;
    const double z = a.z - b.z;
    const double vx = (end_a.x - a.x) - (end_b.x - b.x);
    const double vz = (end_a.z - a.z) - (end_b.z - b.z);
    const double speed = vx * vx + vz * vz;
    const double t = speed == 0 ? 0 : std::clamp(-(x * vx + z * vz) / speed, 0.0, 1.0);
    return std::hypot(x + t * vx, z + t * vz) > clearance;
}
}

bool advance_crowd(const Grid& grid, std::vector<Agent>& agents, const CrowdConfig& config,
                   std::uint64_t tick, std::string& error)
{
    if (config.max_agents == 0 || agents.size() > config.max_agents
        || !std::isfinite(config.radius) || config.radius < 0 || !std::isfinite(2 * config.radius)) {
        error = "invalid crowd limits";
        return false;
    }
    const double clearance = 2 * config.radius;
    for (std::size_t i = 0; i < agents.size(); ++i) {
        if (grid.query(agents[i].position, agents[i].position, config.radius).status != Status::reached) {
            error = "invalid or blocked agent start";
            return false;
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (agents[i].id == agents[j].id
                || !separated(agents[i].position, agents[i].position, agents[j].position,
                               agents[j].position, clearance)) {
                error = "duplicate ID or overlapping agents";
                return false;
            }
        }
    }
    auto output = agents;
    std::vector<std::size_t> order(agents.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::sort(order.begin(), order.end(), [&](auto a, auto b) {
        return std::pair{priority(agents[a].id ^ config.seed ^ priority(tick)), agents[a].id}
            < std::pair{priority(agents[b].id ^ config.seed ^ priority(tick)), agents[b].id};
    });
    for (const auto i : order) {
        const auto route = grid.query(agents[i].position, agents[i].goal, config.radius);
        if (route.status != Status::reached || route.points.size() < 2) {
            continue;
        }
        const Point proposal = route.points[1];
        bool clear = true;
        for (std::size_t j = 0; j < agents.size(); ++j) {
            if (i == j) { continue; }
            if (!separated(agents[i].position, proposal, agents[j].position, agents[j].position, clearance)
                || !separated(agents[i].position, proposal, agents[j].position, output[j].position, clearance)) {
                clear = false;
                break;
            }
        }
        if (clear) { output[i].position = proposal; }
    }
    agents.swap(output);
    error.clear();
    return true;
}
}
