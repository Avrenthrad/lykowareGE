#pragma once

#include <lykoware/mesh.hpp>

#include <cstdint>
#include <span>
#include <string>

// Terrain drawn from a grid of heights (ADR 0095). The mesh is built on the CPU and drawn like
// any other (Renderer::add_mesh). It reads the same layout as physics::HeightFieldShape, and
// splits each cell along the same diagonal, so the drawn ground is the ground bodies stand on.
// Drawing does not create or need a physics body, and the two are freed separately.

namespace lykoware::render {

// A square grid of samples_per_side x samples_per_side heights in metres, row-major: the sample
// at column x, row z is heights[z * samples_per_side + x], at {x * spacing, height, z * spacing}
// in the mesh's own space.
struct TerrainPatch {
    std::span<const float> heights;
    std::uint32_t samples_per_side = 0;
    // Metres between neighbouring samples, above 0.
    float spacing = 1.F;
    // Use every step-th sample, for a coarser mesh over the same ground (a far patch, say).
    // samples_per_side - 1 must be a multiple of it.
    std::uint32_t step = 1;
    // Metres of ground one repeat of a texture covers, above 0.
    float texture_metres = 1.F;
};

// Builds the mesh: one vertex per used sample, two triangles per cell, facing up. Each cell
// from (x, z) to (x + 1, z + 1) is split along that diagonal. Normals come from the height
// difference between the neighbouring used samples (one-sided at an edge). Texture coordinates
// are the position's x and z divided by texture_metres.
//
// False, with the reason in error and the mesh untouched, for: fewer than 2 samples per side,
// a height count other than samples_per_side squared, a step of 0 or one that does not divide
// samples_per_side - 1, a spacing or texture_metres not finite or not above 0, a height that is
// not finite, or a mesh past mesh_max_vertices or mesh_max_indices.
[[nodiscard]] bool terrain_mesh(const TerrainPatch& patch, MeshData& mesh, std::string& error);

}
