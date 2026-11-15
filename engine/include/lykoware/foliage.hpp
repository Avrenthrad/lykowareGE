#pragma once

#include <lykoware/math.hpp>
#include <lykoware/mesh.hpp>
#include <lykoware/terrain_mesh.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

// Foliage drawn as a batch (ADR 0096): many copies of one small mesh (a tuft, a bush, a rock)
// baked into a single mesh on the CPU, so a patch of ground draws its plants in one draw. Where
// the plants stand comes from a seeded scatter over a terrain patch. Drawing only: plants have no
// collision, and which patches exist is the streamer's business.

namespace lykoware::render {

// One plant: where its mesh's origin goes in the patch's own space, a turn about +Y in radians,
// and a uniform scale above 0.
struct FoliageInstance {
    math::Vec3 position{};
    float yaw = 0.F;
    float scale = 1.F;
};

// The most plants one scatter or one batch may hold.
inline constexpr std::size_t foliage_max_instances = std::size_t{1} << 20U;

struct FoliageScatter {
    // The ground to stand on. Its step and texture_metres are ignored: plants stand on the full
    // field, on the surface terrain_mesh draws at step 1.
    TerrainPatch ground;
    // Plants per square metre of ground (measured flat), above 0.
    float density = 1.F;
    // The same seed and ground give the same plants, on every machine.
    std::uint64_t seed = 0;
    // No plant where the ground is steeper than this, radians from level, 0 .. pi/2.
    float max_slope = 0.7F;
    // Each plant's scale is drawn evenly from this range; 0 < min_scale <= max_scale.
    float min_scale = 0.8F;
    float max_scale = 1.2F;
};

// Fills `instances` with plants standing on the ground: each cell of the field gets its share of
// the density, placed and turned by a generator seeded from `seed` and the cell, so a cell's
// plants do not depend on the rest of the field. Positions are exact points of the drawn surface.
//
// False, with the reason in error and `instances` untouched, for: a ground terrain_mesh would
// refuse, a density, slope or scale range outside the ranges above or not finite, or more than
// foliage_max_instances plants.
[[nodiscard]] bool scatter_foliage(const FoliageScatter& scatter, std::vector<FoliageInstance>& instances,
                                   std::string& error);

// Bakes one copy of `plant` per instance into `batch`: scaled, turned about +Y, then moved to the
// instance's position. Normals turn with it; texture coordinates are copied when the plant has
// them. The copies keep the order of `instances`.
//
// False, with the reason in error and `batch` untouched, for: a plant valid_mesh refuses, no
// instances or more than foliage_max_instances, an instance with a position or yaw not finite or
// a scale not finite or not above 0, or a batch past mesh_max_vertices or mesh_max_indices.
[[nodiscard]] bool foliage_batch(const MeshData& plant, std::span<const FoliageInstance> instances, MeshData& batch,
                                 std::string& error);

}
