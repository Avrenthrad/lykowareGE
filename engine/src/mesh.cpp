#include <lykoware/mesh.hpp>

#include <algorithm>
#include <cmath>
#include <string>

namespace lykoware {

namespace {

bool finite(math::Vec3 v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

}

bool valid_mesh(const MeshData& mesh, std::string& error)
{
    if (mesh.positions.empty() || mesh.indices.empty()) {
        error = "a mesh needs at least one triangle";
        return false;
    }
    if (mesh.positions.size() > mesh_max_vertices || mesh.indices.size() > mesh_max_indices) {
        error = "a mesh may have at most " + std::to_string(mesh_max_vertices) + " vertices and "
            + std::to_string(mesh_max_indices) + " indices";
        return false;
    }
    if (mesh.normals.size() != mesh.positions.size()) {
        error = "a mesh needs one normal per position";
        return false;
    }
    if (!mesh.uvs.empty() && mesh.uvs.size() != mesh.positions.size()) {
        error = "a mesh needs no texture coordinates or one per position";
        return false;
    }
    if (!std::all_of(mesh.uvs.begin(), mesh.uvs.end(), [](math::Vec2 uv) { return std::isfinite(uv.x) && std::isfinite(uv.y); })) {
        error = "a mesh texture coordinate is not finite";
        return false;
    }
    if (mesh.indices.size() % 3 != 0) {
        error = "a mesh's indices must be whole triangles";
        return false;
    }
    const auto vertices = static_cast<std::uint32_t>(mesh.positions.size());
    if (std::any_of(mesh.indices.begin(), mesh.indices.end(), [&](std::uint32_t i) { return i >= vertices; })) {
        error = "a mesh index is past the last vertex";
        return false;
    }
    if (!std::all_of(mesh.positions.begin(), mesh.positions.end(), finite)) {
        error = "a mesh position is not finite";
        return false;
    }
    for (const math::Vec3 n : mesh.normals) {
        const float length = math::length(n);
        if (!finite(n) || !(std::abs(length - 1.F) <= 1e-3F)) {
            error = "a mesh normal is not of unit length";
            return false;
        }
    }
    return true;
}

Bounds bounds(const MeshData& mesh)
{
    if (mesh.positions.empty()) {
        return {};
    }
    Bounds box{mesh.positions.front(), mesh.positions.front()};
    for (const math::Vec3 p : mesh.positions) {
        box.min = {std::min(box.min.x, p.x), std::min(box.min.y, p.y), std::min(box.min.z, p.z)};
        box.max = {std::max(box.max.x, p.x), std::max(box.max.y, p.y), std::max(box.max.z, p.z)};
    }
    return box;
}

}
