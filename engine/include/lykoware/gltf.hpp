#pragma once

#include <lykoware/mesh.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Lykoware's glTF 2.0 reader (ADR 0035). Reads a model's shape: every triangle mesh in its
// default scene, with each node's transform applied, merged into one MeshData. Materials,
// textures, skins, morph targets, animation and cameras are ignored for now.
//
// Read: .gltf (JSON) and .glb (binary) files; buffers inside a .glb, in base64 data URIs, or in
// separate files; float positions and normals (flat normals are made when a primitive has
// none); 8-, 16- and 32-bit indices; triangle lists, strips and fans (points and lines are
// skipped); node hierarchies with matrices or translation/rotation/scale. Mirroring transforms
// keep triangles front-facing.
//
// Refused, with a reason: anything malformed or out of bounds, glTF versions other than 2.x,
// required extensions (Draco and meshopt compression among them), sparse accessors, and node
// graphs that are not trees.

namespace lykoware::gltf {

// Reads an external buffer from its URI (already percent-decoded); nullopt when it cannot.
using BufferReader = std::function<std::optional<std::vector<std::byte>>(std::string_view uri)>;

// The largest model file, or external buffer, that is read: 1 GiB.
inline constexpr std::uintmax_t max_file_bytes = std::uintmax_t{1} << 30U;

// From the bytes of a .gltf or .glb file (told apart by the .glb header). Buffers in other files
// are read through `external`; when it is empty, such a model is refused. Fails (nullopt, error
// filled) for any reason above, or when the scene has no triangles.
[[nodiscard]] std::optional<MeshData> read(std::span<const std::byte> file, const BufferReader& external,
                                           std::string& error);

// From a file. External buffers must be relative paths to files in the model's folder or below
// it; absolute paths, "..", and other URI schemes are refused.
[[nodiscard]] std::optional<MeshData> load(const std::filesystem::path& file, std::string& error);

}
