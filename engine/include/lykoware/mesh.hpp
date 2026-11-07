#pragma once

#include <lykoware/math.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Triangle meshes on the CPU (ADR 0035): what model files load into and what the renderer
// uploads. Units and conventions are Lykoware's (ADR 0024): metres, right-handed, +Y up, and
// triangles counter-clockwise when seen from the front.

namespace lykoware {

struct MeshData {
    std::vector<math::Vec3> positions;
    // One unit-length normal per position.
    std::vector<math::Vec3> normals;
    // Three per triangle, each less than positions.size().
    std::vector<std::uint32_t> indices;
    // Texture coordinates: empty, or one per position. (0, 0) is the texture's top-left
    // corner, as in glTF. Without them every vertex reads texture coordinate (0, 0).
    std::vector<math::Vec2> uvs;
};

// The largest mesh accepted: 8,388,608 vertices and 50,331,648 indices (16,777,216 triangles).
// Each fits in one RHI buffer (rhi::buffer_max_bytes).
inline constexpr std::size_t mesh_max_vertices = std::size_t{1} << 23U;
inline constexpr std::size_t mesh_max_indices = std::size_t{3} << 24U;

// True when the mesh can be drawn: at least one triangle, a normal per position, no texture
// coordinates or one per position, whole triangles, indices in range, every value finite and
// every normal of length 1 (within float rounding), within the limits above. Otherwise false,
// with the reason in error.
[[nodiscard]] bool valid_mesh(const MeshData& mesh, std::string& error);

// The smallest box around every position; both corners are zero for an empty mesh.
struct Bounds {
    math::Vec3 min;
    math::Vec3 max;
};
[[nodiscard]] Bounds bounds(const MeshData& mesh);

}
