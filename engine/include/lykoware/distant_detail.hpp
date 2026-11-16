#pragma once

#include <lykoware/math.hpp>
#include <lykoware/mesh.hpp>
#include <lykoware/streaming.hpp>
#include <lykoware/terrain_mesh.hpp>

#include <cstdint>
#include <string>

// Distant chunks drawn with a coarser mesh (ADR 0105). A chunk's detail is picked from the
// camera's distance to its region, measured in double precision before anything becomes float
// (ADR 0021), so a chunk 16 km out switches at the same distance as one at the origin. A band
// of hysteresis keeps a camera on the boundary from flipping the chunk every frame.
//
// Pure functions: no renderer, GPU or streamer state. The caller owns the mesh it builds and
// uploads it with Renderer::add_mesh, placing it at chunk_origin like any other world position.

namespace lykoware::render {

enum class ChunkDetail { near, far };

struct ChunkDetailBands {
    // Metres from the camera to the region's nearest edge where near becomes far. Above 0.
    double far_from_metres = 512.0;
    // A far chunk becomes near again only inside far_from_metres - hysteresis_metres.
    // At least 0 and below far_from_metres.
    double hysteresis_metres = 32.0;
    // TerrainPatch::step used for a far chunk. Above 1.
    std::uint32_t far_step = 4;
};

// Ground distance (x and z) in metres from eye to the nearest point of the region; 0 inside it.
[[nodiscard]] double chunk_distance(streaming::Region region, const math::DVec3& eye);

// The region's corner (min x, 0, min z) as a world position: where a chunk's terrain mesh,
// whose sample (0, 0) is at its own origin, is placed.
[[nodiscard]] math::DVec3 chunk_origin(streaming::Region region);

// The detail for a chunk at distance metres that was previous last frame (near for a new
// chunk). False, with the reason in error and detail untouched, for bands that break the rules
// above or a distance that is negative or not finite.
[[nodiscard]] bool choose_chunk_detail(const ChunkDetailBands& bands, double distance,
                                       ChunkDetail previous, ChunkDetail& detail, std::string& error);

// Builds the chunk's terrain at the given detail: patch.step is replaced by 1 for near and by
// bands.far_step for far. Every far vertex sits exactly on a near vertex, including the
// corners, but a near edge has samples between them, so a near chunk beside a far one can show
// a gap where the ground bends between those samples (skirts are deferred, ADR 0105). Fails as
// terrain_mesh does, and for a far_step that does not divide samples_per_side - 1.
[[nodiscard]] bool chunk_terrain_mesh(TerrainPatch patch, ChunkDetail detail,
                                      const ChunkDetailBands& bands, MeshData& mesh, std::string& error);

}
