#include <lykoware/navigation.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>

namespace {
namespace nav = lykoware::navigation;
int failures = 0;
void expect(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

// Independent segment/AABB slab test, including contact with radius-inflated bounds.
bool intersects(nav::Point a, nav::Point b, nav::Solid solid, double radius)
{
    double enter = 0;
    double leave = 1;
    const auto slab = [&](double start, double finish, double low, double high) {
        const double delta = finish - start;
        if (delta == 0) { return start >= low && start <= high; }
        double first = (low - start) / delta;
        double last = (high - start) / delta;
        if (first > last) { std::swap(first, last); }
        enter = std::max(enter, first);
        leave = std::min(leave, last);
        return enter <= leave;
    };
    return slab(a.x, b.x, solid.min.x - radius, solid.max.x + radius)
        && slab(a.z, b.z, solid.min.z - radius, solid.max.z + radius);
}

bool avoids(const nav::Route& route, nav::Solid solid, double radius)
{
    if (route.status != nav::Status::reached || route.points.empty()) { return false; }
    for (std::size_t i = 1; i < route.points.size(); ++i) {
        if (intersects(route.points[i - 1], route.points[i], solid, radius)) { return false; }
    }
    return true;
}
}

int main(int argc, char** argv)
{
    std::string error;
    auto grid = nav::Grid::create({{0, 0}, 1, 12, 10}, error);
    expect(grid.has_value(), "explicit bounded grid is created");
    if (!grid) { return EXIT_FAILURE; }
    const nav::Point from{1.5, 4.5};
    const nav::Point to{10.5, 4.5};
    const nav::Solid block{{4.2, 3.2}, {6.8, 5.8}};
    expect(grid->upsert(1, block), "add block");
    auto route = grid->query(from, to, 0.25);
    if (argc == 2 && std::string_view(argv[1]) == "--wrong-shortcut") {
        route.points = {from, to};
        // WILL_FAIL: a straight line through the block must fail the route validator.
        return avoids(route, block, 0.25) ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    expect(argc == 1, "no unknown test arguments");
    expect(avoids(route, block, 0.25), "every detour segment clears the inflated block");
    expect(route.points.size() > 2 && route.points.front() == from && route.points.back() == to,
           "detour keeps exact endpoints and intermediate waypoints");
    expect(grid->query(from, to, 0.25).points == route.points, "query repeats exactly");
    const nav::Solid extension{{4.2, 6.2}, {6.8, 9.8}};
    expect(grid->upsert(2, extension), "add solid on previous detour");
    const auto changed = grid->query(from, to, 0.25);
    expect(changed.points != route.points && avoids(changed, block, 0.25) && avoids(changed, extension, 0.25),
           "added solid changes the route immediately without a shortcut");
    expect(grid->erase(2) && !grid->erase(2), "erase solid once");
    expect(grid->query(from, to, 0.25).points == route.points, "removal restores the original route");
    expect(!grid->upsert(1, {{7, 2}, {3, 4}}) && !grid->upsert(0, block), "invalid solid update is refused");
    expect(grid->query(from, to, 0.25).points == route.points, "invalid replacement preserves original solid");
    expect(grid->query({3.5, 4.5}, {2.5, 4.5}, 0).status == nav::Status::reached,
           "point agent fits beside block");
    expect(grid->query({3.5, 4.5}, {2.5, 4.5}, 0.4).status == nav::Status::unreachable,
           "radius clearance blocks a cell too near the solid");
    auto other = nav::Grid::create({{0, 0}, 1, 12, 10}, error);
    if (!other) { return EXIT_FAILURE; }
    expect(other->upsert(2, extension) && other->upsert(1, block) && grid->upsert(2, extension),
           "populate the same solids in opposite insertion orders");
    expect(other->query(from, to, 0.25).points == grid->query(from, to, 0.25).points,
           "solid insertion order does not change the route");
    expect(grid->upsert(3, {{5.2, -1}, {5.8, 11}}), "add full blocking wall");
    const auto blocked = grid->query(from, to, 0.25);
    expect(blocked.status == nav::Status::unreachable && blocked.points.empty(), "blocked route has no partial path");
    expect(grid->upsert(3, {{8.2, 8.2}, {8.8, 8.8}}), "replace wall with a small distant solid");
    expect(grid->query(from, to, 0.25).status == nav::Status::reached, "valid replacement changes occupancy");
    expect(grid->erase(3), "remove full wall");
    expect(grid->query(from, to, 0.25).status == nav::Status::reached, "route recovers after wall removal");
    expect(grid->query({0.5, 0.5}, {0.5, 0.5}, 0).status == nav::Status::reached, "same cell is reachable");
    expect(grid->query({0.5, 0.5}, to, 0.1).status == nav::Status::unreachable, "edge cell reserves radius clearance");
    expect(grid->query({12, 4}, to, 0).status == nav::Status::invalid_input, "maximum boundary is outside");
    expect(grid->query({0, 0}, {1, 1}, 0).status == nav::Status::reached, "minimum boundary is inside");
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    expect(grid->query({nan, 0}, to, 0).status == nav::Status::invalid_input
               && grid->query(from, to, -1).status == nav::Status::invalid_input
               && grid->query(from, to, nan).points.empty(), "invalid endpoints/radii are refused");
    expect(!grid->upsert(1, {{nan, 0}, {5, 5}}), "nonfinite solid is refused");
    expect(!nav::Grid::create({{0, 0}, 0, 12, 10}, error)
               && !nav::Grid::create({{0, 0}, 1, 65537, 1}, error)
               && !nav::Grid::create({{1e300, 0}, 1, 12, 10}, error), "invalid/collapsed configurations are refused");
    auto distant = nav::Grid::create({{16000, -16000}, 1, 12, 10}, error);
    expect(distant && distant->query({16001.5, -15995.5}, {16010.5, -15995.5}, 0).status == nav::Status::reached,
           "double coordinates work beyond eight kilometres");
    if (failures != 0) { return EXIT_FAILURE; }
    std::cout << "navigation passed\n";
    return EXIT_SUCCESS;
}
