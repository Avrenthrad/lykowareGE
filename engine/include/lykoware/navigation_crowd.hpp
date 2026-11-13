#pragma once

#include <lykoware/navigation.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lykoware::navigation {
struct Agent {
    std::uint64_t id;
    Point position;
    Point goal;
};
struct CrowdConfig {
    std::size_t max_agents;
    double radius;
    std::uint64_t seed;
};

// GPT-owned ADR 0078: one simultaneous waypoint reservation tick, deterministic
// for the same grid/agents/config/tick in one build. Caller owns all bytes; no
// physics/ECS access. Single simulation thread. False + error leaves agents
// unchanged on invalid input; allocation exceptions also leave them unchanged.
// Unreachable goals wait. Conservative reservations may deadlock; no arrival
// guarantee. Interpolate accepted segments at the same normalized tick time.
[[nodiscard]] bool advance_crowd(const Grid& grid, std::vector<Agent>& agents,
                                const CrowdConfig& config, std::uint64_t tick, std::string& error);
}
