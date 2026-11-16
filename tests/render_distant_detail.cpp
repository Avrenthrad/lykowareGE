// render.distant_detail (ADR 0105): distant chunks drawn with a coarser terrain mesh.
//
// CPU only. Checks that a near chunk gets the full mesh and a far one a coarser mesh that is not
// a full-detail duplicate, that every far vertex lies on a near one, that the switch has
// hysteresis, that distances are measured in double precision 16 km out exactly as at the
// origin (ADR 0021), and that bad bands and distances are refused.
//
// Exit codes: 0 pass, 1 fail.

#include <lykoware/distant_detail.hpp>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* what)
{
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

using lykoware::render::ChunkDetail;
using lykoware::render::ChunkDetailBands;

std::vector<float> rolling_heights(std::uint32_t side)
{
    std::vector<float> heights(static_cast<std::size_t>(side) * side);
    for (std::uint32_t z = 0; z < side; ++z) {
        for (std::uint32_t x = 0; x < side; ++x) {
            heights[static_cast<std::size_t>(z) * side + x] =
                3.F * std::sin(0.11F * static_cast<float>(x)) + 2.F * std::cos(0.07F * static_cast<float>(z));
        }
    }
    return heights;
}

bool same(const lykoware::math::Vec3& a, const lykoware::math::Vec3& b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

void representations()
{
    constexpr std::uint32_t side = 65; // 64 cells of 4 m: one 256 m region.
    const std::vector<float> heights = rolling_heights(side);
    lykoware::render::TerrainPatch patch;
    patch.heights = heights;
    patch.samples_per_side = side;
    patch.spacing = 4.F;
    patch.step = 8; // Ignored: detail decides the step.
    const ChunkDetailBands bands; // far_step 4.

    lykoware::MeshData near_mesh;
    lykoware::MeshData far_mesh;
    std::string error;
    check(lykoware::render::chunk_terrain_mesh(patch, ChunkDetail::near, bands, near_mesh, error), "near mesh builds");
    check(lykoware::render::chunk_terrain_mesh(patch, ChunkDetail::far, bands, far_mesh, error), "far mesh builds");

    check(near_mesh.positions.size() == side * side, "near mesh uses every sample");
    check(far_mesh.positions.size() == 17U * 17U, "far mesh uses every 4th sample");
    check(far_mesh.indices.size() * 16 == near_mesh.indices.size(), "far mesh has a 16th of the triangles");
    check(far_mesh.positions.size() < near_mesh.positions.size(), "far mesh is not a full-detail duplicate");

    // Every far vertex is a near vertex: far (i, j) is near (4i, 4j).
    bool on_near = far_mesh.positions.size() == 17U * 17U;
    for (std::uint32_t z = 0; on_near && z < 17; ++z) {
        for (std::uint32_t x = 0; on_near && x < 17; ++x) {
            on_near = same(far_mesh.positions[z * 17 + x], near_mesh.positions[(z * 4) * side + x * 4]);
        }
    }
    check(on_near, "every far vertex lies exactly on a near vertex");

    ChunkDetailBands uneven;
    uneven.far_step = 5; // 64 is not a multiple of 5.
    lykoware::MeshData untouched;
    check(!lykoware::render::chunk_terrain_mesh(patch, ChunkDetail::far, uneven, untouched, error)
              && untouched.positions.empty(),
          "far_step that does not divide the grid is refused");
}

ChunkDetail step(const ChunkDetailBands& bands, double distance, ChunkDetail previous)
{
    ChunkDetail detail = previous;
    std::string error;
    check(lykoware::render::choose_chunk_detail(bands, distance, previous, detail, error), "detail chosen");
    return detail;
}

void hysteresis()
{
    ChunkDetailBands bands;
    bands.far_from_metres = 500.0;
    bands.hysteresis_metres = 50.0;

    check(step(bands, 0.0, ChunkDetail::near) == ChunkDetail::near, "inside region is near");
    check(step(bands, 499.0, ChunkDetail::near) == ChunkDetail::near, "just short of far stays near");
    check(step(bands, 500.0, ChunkDetail::near) == ChunkDetail::far, "at far_from becomes far");
    check(step(bands, 499.0, ChunkDetail::far) == ChunkDetail::far, "far stays far inside the band");
    check(step(bands, 450.0, ChunkDetail::far) == ChunkDetail::far, "far stays far at the band edge");
    check(step(bands, 449.9, ChunkDetail::far) == ChunkDetail::near, "far becomes near past the band");

    // A camera jittering across far_from flips at most once.
    ChunkDetail detail = ChunkDetail::near;
    int flips = 0;
    for (int frame = 0; frame < 120; ++frame) {
        const double distance = 500.0 + ((frame % 2 == 0) ? 3.0 : -3.0);
        const ChunkDetail next = step(bands, distance, detail);
        flips += next != detail ? 1 : 0;
        detail = next;
    }
    check(flips == 1, "boundary jitter switches detail once, not every frame");

    ChunkDetailBands no_band = bands;
    no_band.hysteresis_metres = 0.0;
    check(step(no_band, 499.0, ChunkDetail::far) == ChunkDetail::near, "zero hysteresis switches back at once");
}

void camera_relative()
{
    using lykoware::math::DVec3;
    using lykoware::streaming::Region;
    using lykoware::streaming::region_size;

    // The same layout at the origin and 16 km out must measure the same.
    const Region home{0, 0};
    const Region away{62, 62}; // Origin at 15,872 m.
    const DVec3 eye_home{-0.125, 1.7, 128.0};
    const DVec3 eye_away{62 * region_size - 0.125, 1.7, 62 * region_size + 128.0};
    check(lykoware::render::chunk_distance(home, eye_home) == 0.125, "distance at origin is exact");
    check(lykoware::render::chunk_distance(away, eye_away) == 0.125, "distance 16 km out is exact");
    check(lykoware::render::chunk_distance(away, DVec3{62 * region_size + 10.0, 0.0, 62 * region_size + 10.0}) == 0.0,
          "inside the region is 0");
    check(lykoware::render::chunk_distance(Region{3, 0}, DVec3{0.0, 0.0, 0.0}) == 3 * region_size,
          "distance to a region three along is to its near edge");

    const DVec3 origin = lykoware::render::chunk_origin(away);
    check(origin.x == 62 * region_size && origin.y == 0.0 && origin.z == 62 * region_size, "origin is the region corner");
    // Made relative to the eye before it becomes float, the offset keeps its sub-metre part.
    const auto relative_x = static_cast<float>(origin.x - eye_away.x);
    check(relative_x == 0.125F, "camera-relative offset keeps sub-metre precision 16 km out");
}

void refusals()
{
    ChunkDetail detail = ChunkDetail::near;
    std::string error;
    auto refused = [&](const ChunkDetailBands& bands, double distance) {
        detail = ChunkDetail::near;
        error.clear();
        const bool ok = lykoware::render::choose_chunk_detail(bands, distance, ChunkDetail::far, detail, error);
        return !ok && !error.empty() && detail == ChunkDetail::near;
    };
    const ChunkDetailBands good;
    ChunkDetailBands bad = good;
    bad.far_from_metres = 0.0;
    check(refused(bad, 10.0), "far_from 0 refused");
    bad = good;
    bad.far_from_metres = std::numeric_limits<double>::infinity();
    check(refused(bad, 10.0), "far_from infinite refused");
    bad = good;
    bad.hysteresis_metres = -1.0;
    check(refused(bad, 10.0), "negative hysteresis refused");
    bad = good;
    bad.hysteresis_metres = good.far_from_metres;
    check(refused(bad, 10.0), "hysteresis as wide as far_from refused");
    bad = good;
    bad.far_step = 1;
    check(refused(bad, 10.0), "far_step 1 (full detail) refused");
    check(refused(good, -1.0), "negative distance refused");
    check(refused(good, std::numeric_limits<double>::quiet_NaN()), "NaN distance refused");
}

}

int main()
{
    representations();
    hysteresis();
    camera_relative();
    refusals();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "render.distant_detail passed\n";
    return EXIT_SUCCESS;
}
