#pragma once

#include <lykoware/ecs.hpp>
#include <lykoware/image_decode.hpp>
#include <lykoware/math.hpp>
#include <lykoware/mesh.hpp>
#include <lykoware/rhi/device.hpp>
#include <lykoware/rhi/pipeline.hpp>
#include <lykoware/rhi/render_target.hpp>
#include <lykoware/rhi/swapchain.hpp>
#include <lykoware/rhi/texture.hpp>
#include <lykoware/transform.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

// The first renderer (ADR 0026): lit meshes for every ECS entity with a Transform and a
// MeshRenderer, drawn camera-relative through the RHI.

namespace lykoware::render {

// Built-in meshes.
enum class MeshShape : std::uint32_t {
    // A cube 1 m on each side, centred on the entity's position.
    Cube,
};

// A mesh uploaded with Renderer::add_mesh (ADR 0035), valid only with the renderer that made
// it: `owner` is that renderer's process-local token (ADR 0030), never to be persisted or
// hashed. The default (value 0) means none.
struct MeshId {
    std::uint32_t value = 0;
    std::uint64_t owner = 0;

    friend bool operator==(const MeshId&, const MeshId&) = default;
};

// A texture uploaded with Renderer::add_texture (ADR 0037), valid only with the renderer that
// made it: `owner` is that renderer's process-local token (ADR 0030), never to be persisted or
// hashed. The default (value 0) means none: the colour alone, as if the texture were white.
struct TextureId {
    std::uint32_t value = 0;
    std::uint64_t owner = 0;

    friend bool operator==(const TextureId&, const TextureId&) = default;
};

// How a texture is stored and sampled.
struct TextureSettings {
    // Colour textures are sRGB; data textures (masks) are not.
    rhi::TextureFormat format = rhi::TextureFormat::Rgba8Srgb;
    rhi::TextureWrap wrap = rhi::TextureWrap::Repeat;
    rhi::TextureFilter filter = rhi::TextureFilter::Linear;
    bool mipmaps = true;
};

// One part of an added mesh with its own material (ADR 0038): the triangles from first_index
// for index_count indices. Its colour multiplies MeshRenderer::colour; its texture, when set,
// takes the place of MeshRenderer::texture.
struct MeshPart {
    std::uint32_t first_index = 0;
    std::uint32_t index_count = 0;
    math::Vec3 colour{1.F, 1.F, 1.F};
    TextureId texture{};
};

// ECS component: draw the entity's Transform with a built-in mesh, or with an added mesh.
struct MeshRenderer {
    MeshShape shape = MeshShape::Cube;
    // Scale along the mesh's own axes; a 1 m cube with scale {2, 1, 1} is 2 m long.
    math::Vec3 scale{1.F, 1.F, 1.F};
    // Base colour, lit by one directional light from above plus ambient.
    math::Vec3 colour{0.8F, 0.8F, 0.8F};
    // When set, drawn instead of `shape`. An id this renderer did not make is not drawn.
    MeshId mesh{};
    // Multiplies `colour`, read at the mesh's texture coordinates. An id this renderer did not
    // make is not drawn.
    TextureId texture{};
};

// Per-instance drawing pose for an uploaded skinned mesh (ADR 0052). Plain CPU matrices;
// copied into the target/frame slot during draw. Missing component draws bind geometry.
// When present, count must match the mesh's joint count; matrices must be finite affine.
// Animation never changes the authoritative Transform or physics state.
struct SkinPose {
    std::vector<math::Mat4> matrices;
};

// Where the view comes from. Positions are world positions in double precision; everything is
// made relative to `eye` before it becomes float, so the view is as sharp 16 km out as at
// the origin (ADR 0021, ADR 0024).
struct Camera {
    math::DVec3 eye{0.0, 2.0, 8.0};
    math::DVec3 target{0.0, 0.0, 0.0};
    math::Vec3 up{0.F, 1.F, 0.F};
    float vertical_fov = 1.0471976F; // 60 degrees
    float near = 0.1F;
};

// Draws ECS scenes. One renderer per colour format.
//
// Ownership: create() returns the sole owner of its pipeline, mesh buffers and textures. Copy
// and move are not provided. Lifetime: destroy it before the Device, and after any Swapchain it drew to
// (the swapchain's destructor waits for the GPU). Threading: the Device's thread.
//
// Failure: create() returns nullptr and fills error (bad SPIR-V, driver refusals). draw()
// returns false and fills error when the RHI refuses or the driver fails.
class Renderer {
public:
    // lit_vertex and lit_fragment are the SPIR-V of engine/shaders/lit.slang's vertexMain and
    // fragmentMain. Optional skinned_vertex is skinned.slang vertexMain; that renderer uses
    // the skinned layout for static and animated geometry. color_format must match the targets the renderer draws to.
    [[nodiscard]] static std::unique_ptr<Renderer> create(
        const rhi::Device& device, rhi::ColorFormat color_format, std::span<const std::uint32_t> lit_vertex,
        std::span<const std::uint32_t> lit_fragment, rhi::RhiError& error,
        std::span<const std::uint32_t> skinned_vertex = {});

    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    // Clears to `clear` and draws every entity with a Transform and a MeshRenderer, in ECS
    // iteration order (deterministic). The target needs a depth buffer.
    [[nodiscard]] bool draw(const ecs::World& entities, const Camera& camera, rhi::RenderTarget& target,
                            const rhi::ClearColor& clear, rhi::RhiError& error);
    // The same into a window. The swapchain needs depth (SwapchainConfig::depth).
    [[nodiscard]] bool draw(const ecs::World& entities, const Camera& camera, rhi::Swapchain& swapchain,
                            const rhi::ClearColor& clear, rhi::RhiError& error);

    // Uploads a mesh (a loaded model, say) for MeshRenderer::mesh. It stays until the renderer
    // is destroyed. Fails (nullopt, error filled) for a mesh valid_mesh() refuses (InvalidArgument)
    // or when the driver refuses the buffers.
    [[nodiscard]] std::optional<MeshId> add_mesh(const MeshData& mesh, rhi::RhiError& error);
    // The same, drawn as parts with their own materials (a glTF model's primitives, say). Each
    // part needs a non-zero, whole-triangle range inside the mesh's indices, and a texture id
    // this renderer made (or none); otherwise InvalidArgument.
    [[nodiscard]] std::optional<MeshId> add_mesh(const MeshData& mesh, std::span<const MeshPart> parts, rhi::RhiError& error);

    // Upload once, draw with SkinPose. Requires create()'s optional skinned vertex shader.
    // Four joint/weight slots per vertex; weights finite/nonnegative, sum 1 within 1e-4;
    // positive-weight indices less than joint_count (1..256). Normalises accepted weights.
    // Parts follow add_mesh's rules; empty parts means the whole mesh. InvalidArgument on
    // bad inputs, unsupported renderer or vertex data beyond the RHI buffer limit.
    [[nodiscard]] std::optional<MeshId> add_skinned_mesh(
        const MeshData& mesh, std::span<const std::array<std::uint16_t, 4>> joints,
        std::span<const math::Vec4> weights, std::size_t joint_count,
        std::span<const MeshPart> parts, rhi::RhiError& error);

    // Uploads a texture (a decoded image, say) for MeshRenderer::texture. It stays until the
    // renderer is destroyed. Fails (nullopt, error filled) for an empty or oversized image
    // (InvalidArgument) or when the driver refuses it.
    [[nodiscard]] std::optional<TextureId> add_texture(const RgbaImage& image, const TextureSettings& settings,
                                                       rhi::RhiError& error);

    // Entities drawn by the last draw() (a mesh of several parts counts once).
    [[nodiscard]] std::size_t last_draw_count() const;

private:
    struct Impl;
    explicit Renderer(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}
