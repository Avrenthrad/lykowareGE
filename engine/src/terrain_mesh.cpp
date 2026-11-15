#include <lykoware/terrain_mesh.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace lykoware::render {

bool terrain_mesh(const TerrainPatch& patch, MeshData& mesh, std::string& error)
{
    const std::size_t side = patch.samples_per_side;
    if (side < 2) {
        error = "terrain needs at least 2 samples per side";
        return false;
    }
    if (patch.heights.size() != side * side) {
        error = "terrain needs samples_per_side squared heights";
        return false;
    }
    if (patch.step == 0 || (side - 1) % patch.step != 0) {
        error = "terrain step must divide samples_per_side - 1";
        return false;
    }
    if (!std::isfinite(patch.spacing) || patch.spacing <= 0.F || !std::isfinite(patch.texture_metres)
        || patch.texture_metres <= 0.F) {
        error = "terrain spacing and texture_metres must be finite and above 0";
        return false;
    }
    if (!std::all_of(patch.heights.begin(), patch.heights.end(), [](float h) { return std::isfinite(h); })) {
        error = "terrain heights must be finite";
        return false;
    }
    const std::size_t step = patch.step;
    const std::size_t cells = (side - 1) / step;
    const std::size_t used = cells + 1;
    if (used * used > mesh_max_vertices || cells * cells * 6 > mesh_max_indices) {
        error = "terrain mesh is past the mesh limits; use a larger step";
        return false;
    }

    MeshData made;
    made.positions.reserve(used * used);
    made.normals.reserve(used * used);
    made.uvs.reserve(used * used);
    made.indices.reserve(cells * cells * 6);
    const auto height = [&](std::size_t x, std::size_t z) { return patch.heights[z * side + x]; };
    for (std::size_t j = 0; j < used; ++j) {
        for (std::size_t i = 0; i < used; ++i) {
            const std::size_t x = i * step;
            const std::size_t z = j * step;
            const float px = static_cast<float>(x) * patch.spacing;
            const float pz = static_cast<float>(z) * patch.spacing;
            made.positions.push_back({px, height(x, z), pz});
            made.uvs.push_back({px / patch.texture_metres, pz / patch.texture_metres});
            // The slope between the neighbouring used samples; one-sided at an edge.
            const std::size_t x0 = i > 0 ? x - step : x;
            const std::size_t x1 = i < cells ? x + step : x;
            const std::size_t z0 = j > 0 ? z - step : z;
            const std::size_t z1 = j < cells ? z + step : z;
            const float slope_x = (height(x1, z) - height(x0, z)) / (static_cast<float>(x1 - x0) * patch.spacing);
            const float slope_z = (height(x, z1) - height(x, z0)) / (static_cast<float>(z1 - z0) * patch.spacing);
            const float length = std::sqrt(slope_x * slope_x + 1.F + slope_z * slope_z);
            made.normals.push_back({-slope_x / length, 1.F / length, -slope_z / length});
        }
    }
    for (std::size_t j = 0; j < cells; ++j) {
        for (std::size_t i = 0; i < cells; ++i) {
            const auto a = static_cast<std::uint32_t>(j * used + i);
            const auto b = static_cast<std::uint32_t>((j + 1) * used + i);
            const auto c = static_cast<std::uint32_t>((j + 1) * used + i + 1);
            const auto d = static_cast<std::uint32_t>(j * used + i + 1);
            made.indices.insert(made.indices.end(), {a, b, c, a, c, d});
        }
    }
    mesh = std::move(made);
    return true;
}

}
