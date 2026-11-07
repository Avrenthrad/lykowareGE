#pragma once

#include <lykoware/animation.hpp>
#include <lykoware/image_decode.hpp>
#include <lykoware/mesh.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Lykoware's glTF 2.0 reader (ADR 0035, ADR 0038). Reads a model's shape: every triangle mesh
// in its default scene, with each node's transform applied, merged into one MeshData. read_model
// and load_model also read texture coordinates (TEXCOORD_0) and base colour materials, with
// their textures decoded (PNG and JPEG, ADR 0036). read_skinned and load_skinned read a skinned
// model with its skeleton and animations (ADR 0050); read and read_model ignore skins and
// animations. Morph targets, cameras and the other material maps are ignored for now.
//
// Read: .gltf (JSON) and .glb (binary) files; buffers inside a .glb, in base64 data URIs, or in
// separate files; float positions and normals (flat normals are made when a primitive has
// none); texture coordinates (float, or normalised 8- or 16-bit); 8-, 16- and 32-bit indices;
// triangle lists, strips and fans (points and lines are skipped); node hierarchies with
// matrices or translation/rotation/scale. Mirroring transforms keep triangles front-facing.
//
// Refused, with a reason: anything malformed or out of bounds, glTF versions other than 2.x,
// required extensions (Draco and meshopt compression among them), sparse accessors, and node
// graphs that are not trees.

namespace lykoware::gltf {

// Reads an external buffer from its URI (already percent-decoded); nullopt when it cannot.
using BufferReader = std::function<std::optional<std::vector<std::byte>>(std::string_view uri)>;

// The largest model file, or external buffer, that is read: 1 GiB.
inline constexpr std::uintmax_t max_file_bytes = std::uintmax_t{1} << 30U;

// The most image pixels one model may decode in all: 268,435,456 (1 GiB as RGBA).
inline constexpr std::uint64_t max_model_image_pixels = std::uint64_t{1} << 28U;

// How a texture is sampled, from its glTF sampler. glTF wraps the two axes separately;
// Lykoware uses the S (horizontal) setting for both for now.
enum class Wrap { Repeat, MirroredRepeat, ClampToEdge };
struct Sampling {
    Wrap wrap = Wrap::Repeat;
    // magFilter NEAREST.
    bool nearest = false;
    // minFilter uses mipmaps (or is unset).
    bool mipmaps = true;
};

// A texture: one of the model's images, with how to sample it.
struct Texture {
    std::size_t image = 0;
    Sampling sampling;
};

// A material's base colour: a factor (linear RGBA, 0..1) times an optional texture (sRGB).
struct Material {
    math::Vec4 base_colour{1.F, 1.F, 1.F, 1.F};
    std::optional<std::size_t> base_colour_texture;
};

// One primitive's triangles within Model::mesh.indices, and its material (none: the glTF
// default material, plain white).
struct Part {
    std::uint32_t first_index = 0;
    std::uint32_t index_count = 0;
    std::optional<std::size_t> material;
};

// A model with its materials. Parts are in scene order; materials, textures and images hold only
// what the parts use, each once.
struct Model {
    MeshData mesh;
    std::vector<Part> parts;
    std::vector<Material> materials;
    std::vector<Texture> textures;
    std::vector<RgbaImage> images;
};

// From the bytes of a .gltf or .glb file (told apart by the .glb header). Buffers in other files
// are read through `external`; when it is empty, such a model is refused. Fails (nullopt, error
// filled) for any reason above, or when the scene has no triangles.
[[nodiscard]] std::optional<MeshData> read(std::span<const std::byte> file, const BufferReader& external,
                                           std::string& error);

// From a file. External buffers must be relative paths to files in the model's folder or below
// it; absolute paths, "..", and other URI schemes are refused.
[[nodiscard]] std::optional<MeshData> load(const std::filesystem::path& file, std::string& error);

// As read() and load(), with materials: the shape, its parts, and the base colour materials,
// textures and images they use. Images may be in a buffer view, a data URI or a separate file
// (read like external buffers). Also refused: a material whose base colour texture uses a
// texture coordinate set other than 0, a factor outside 0..1, a texture without a source image,
// an unknown sampler setting, an image that is not PNG or JPEG or does not decode, and images
// over max_model_image_pixels in all.
[[nodiscard]] std::optional<Model> read_model(std::span<const std::byte> file, const BufferReader& external,
                                              std::string& error);
[[nodiscard]] std::optional<Model> load_model(const std::filesystem::path& file, std::string& error);

// A skinned, animated model (ADR 0050).
struct SkinnedModel {
    // The mesh in bind space (the skinned mesh nodes' own transforms are ignored, as glTF
    // requires), with its parts and base colour materials as read_model gives them.
    Model model;
    // For each vertex of model.mesh: up to four skeleton joints and their weights, which sum to 1
    // (normalised on reading); an unused slot has weight 0.
    std::vector<std::array<std::uint16_t, 4>> joints;
    std::vector<math::Vec4> weights;
    // The skin's joints and every node above them, parents first, so transforms above the joints
    // (an exporter's armature node, say) are kept. A skin joint's inverse bind matrix is the
    // skin's (the identity when the skin gives none); the nodes above have the identity.
    animation::Skeleton skeleton;
    // The model's animations, one clip each, named as in the file ("animation N" when unnamed).
    // Channels for nodes outside the skeleton and morph-target weights are left out.
    std::vector<animation::Clip> clips;
};

// As read_model, for a model whose meshes are skinned. Also refused: a model with no skin or more
// than one; a mesh in the scene without the skin; a skinned primitive without JOINTS_0 and
// WEIGHTS_0, or with JOINTS_1 (more than four joints a vertex); a joint index outside the skin;
// negative weights or weights summing to 0; more than animation::max_joints skeleton joints; a
// skeleton node whose matrix is not translation, rotation and scale; fewer inverse bind matrices
// than joints; and animations that would not sample (animation::valid_clip), have an unknown
// interpolation or path, or keys in an unsupported form. Joint indices may be unsigned 8- or
// 16-bit; weights and rotation keys float or normalised integers, as glTF allows.
[[nodiscard]] std::optional<SkinnedModel> read_skinned(std::span<const std::byte> file, const BufferReader& external,
                                                       std::string& error);
[[nodiscard]] std::optional<SkinnedModel> load_skinned(const std::filesystem::path& file, std::string& error);

}
