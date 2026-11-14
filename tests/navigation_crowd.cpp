#include <lykoware/navigation_crowd.hpp>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
namespace nav = lykoware::navigation;
void require(bool value, const char* message)
{
    if (!value) { throw std::runtime_error(message); }
}
bool clear(const std::vector<nav::Agent>& before, const std::vector<nav::Agent>& after, double radius)
{
    // Independent oracle samples every accepted segment, including endpoints.
    for (int sample = 0; sample <= 20; ++sample) {
        const double t = static_cast<double>(sample) / 20;
        for (std::size_t i = 0; i < after.size(); ++i) {
            const auto a = nav::Point{std::lerp(before[i].position.x, after[i].position.x, t),
                                      std::lerp(before[i].position.z, after[i].position.z, t)};
            for (std::size_t j = 0; j < i; ++j) {
                const auto b = nav::Point{std::lerp(before[j].position.x, after[j].position.x, t),
                                          std::lerp(before[j].position.z, after[j].position.z, t)};
                if (std::hypot(a.x - b.x, a.z - b.z) <= 2 * radius) { return false; }
            }
        }
    }
    return true;
}
std::vector<nav::Agent> run(const nav::Grid& grid, bool break_reservation)
{
    std::vector<nav::Agent> agents;
    for (std::uint64_t i = 0; i < 256; ++i) {
        agents.push_back({i, {2.5 + static_cast<double>(i % 16), 2.5 + static_cast<double>(i / 16)}, {25.5, 25.5}});
    }
    const auto started = std::chrono::steady_clock::now();
    std::string error;
    bool moved = false;
    for (std::uint64_t tick = 0; tick < 32; ++tick) {
        const auto before = agents;
        require(nav::advance_crowd(grid, agents, {256, 0.2, 0x1234}, tick, error), "crowd advances");
        if (break_reservation) { agents[1].position = agents[0].position; }
        require(clear(before, agents, 0.2), "overlapping goals preserve swept separation");
        for (std::size_t i = 0; i < agents.size(); ++i) {
            moved = moved || agents[i].position != before[i].position;
        }
    }
    require(moved, "crowd fixture must move agents, not pass by freezing all work");
    std::cout << "agents=256 ticks=32 elapsed_us="
              << std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count()
              << " budget=open(01.11,05.09)\n";
    return agents;
}
}
int main(int argc, char** argv)
{
    try {
        std::string error;
        const auto grid = nav::Grid::create({{0, 0}, 1, 32, 32}, error);
        require(grid.has_value(), "grid created");
        const auto first = run(*grid, argc == 2 && std::string_view(argv[1]) == "--overlap");
        const auto second = run(*grid, false);
        for (std::size_t i = 0; i < first.size(); ++i) {
            require(std::bit_cast<std::uint64_t>(first[i].position.x) == std::bit_cast<std::uint64_t>(second[i].position.x)
                && std::bit_cast<std::uint64_t>(first[i].position.z) == std::bit_cast<std::uint64_t>(second[i].position.z),
                    "fixed seed repeats bit for bit");
        }
        std::vector<nav::Agent> pair{{1, {4.5, 4.5}, {5.5, 4.5}}, {2, {5.5, 4.5}, {4.5, 4.5}}};
        const auto before = pair;
        require(nav::advance_crowd(*grid, pair, {2, 0.2, 7}, 0, error) && clear(before, pair, 0.2), "no edge swap");
        pair[1].id = pair[0].id;
        require(!nav::advance_crowd(*grid, pair, {2, 0.2, 7}, 0, error), "duplicate IDs refused");
        pair = before;
        auto reverse = before;
        std::reverse(reverse.begin(), reverse.end());
        require(nav::advance_crowd(*grid, pair, {2, 0.2, 7}, 3, error)
            && nav::advance_crowd(*grid, reverse, {2, 0.2, 7}, 3, error), "permutation runs");
        require(pair[0].position == reverse[1].position && pair[1].position == reverse[0].position, "ID priority independent of order");
        require(!nav::advance_crowd(*grid, pair, {1, 0.2, 7}, 0, error), "count limit enforced");
        require(!nav::advance_crowd(*grid, pair, {2, std::numeric_limits<double>::quiet_NaN(), 7}, 0, error), "invalid radius refused");
        pair = before;
        pair[1].position = pair[0].position;
        const auto overlap = pair;
        require(!nav::advance_crowd(*grid, pair, {2, 0.2, 7}, 0, error)
            && pair[0].position == overlap[0].position && pair[1].position == overlap[1].position,
                "initial overlap refused without mutation");
        auto changed = *grid;
        require(changed.upsert(5, {{4.1, 4.1}, {4.9, 4.9}}), "solid added");
        pair = before;
        require(!nav::advance_crowd(changed, pair, {2, 0.2, 7}, 0, error), "changed solid blocks occupied start");
        pair = {{1, {2.5, 2.5}, {100, 100}}};
        require(nav::advance_crowd(*grid, pair, {1, 0.2, 7}, 0, error) && pair[0].position == nav::Point{2.5, 2.5},
                "invalid goal waits safely");
        std::cout << "crowd passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
