#pragma once

#include <lykoware/math.hpp>
#include <lykoware/mesh.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace lykoware::animation {

// CPU reference linear-blend skinning (ADR 0051), for captures, offline tools and comparison
// with a future GPU implementation. Not a production per-frame upload path.
//
// bind must pass valid_mesh(); joints and weights have one entry per vertex; matrices contains
// 1..max_joints finite affine skinning matrices (from skinning_matrices(), for example).
// Weights must be finite, non-negative, and sum to 1 within 1e-4; they are normalised before
// blending. Zero-weight slots are ignored; every positive-weight joint must exist.
//
// Positions use the weighted matrix; normals use its inverse transpose, normalised. Singular
// blends, non-finite/unrepresentable results and invalid inputs fail with a reason. Triangle
// indices and UVs are preserved. No winding repair or recomputation of surface normals occurs.
// On false, out is unchanged. On success, out is replaced; out may alias bind. Allocation
// exceptions propagate, leaving out unchanged. Does not mutate skeletons, poses or simulation.
// Threading: pure CPU data; separate outputs may be processed independently.
[[nodiscard]] bool deform_mesh(const MeshData& bind,
                               std::span<const std::array<std::uint16_t, 4>> joints,
                               std::span<const math::Vec4> weights,
                               std::span<const math::Mat4> matrices,
                               MeshData& out, std::string& error);

}
