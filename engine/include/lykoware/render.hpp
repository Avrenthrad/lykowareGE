#pragma once

#include <lykoware/ecs.hpp>
#include <lykoware/math.hpp>
#include <lykoware/mesh.hpp>
#include <lykoware/rhi/device.hpp>
#include <lykoware/rhi/pipeline.hpp>
#include <lykoware/rhi/render_target.hpp>
#include <lykoware/rhi/swapchain.hpp>
#include <lykoware/transform.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

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

// ECS component: draw the entity's Transform with a built-in mesh, or with an added mesh.
struct MeshRenderer {
    MeshShape shape = MeshShape::Cube;
    // Scale along the mesh's own axes; a 1 m cube with scale {2, 1, 1} is 2 m long.
    math::Vec3 scale{1.F, 1.F, 1.F};
    // Base colour, lit by one directional light from above plus ambient.
    math::Vec3 colour{0.8F, 0.8F, 0.8F};
    // When set, drawn instead of `shape`. An id this renderer did not make is not drawn.
    MeshId mesh{};
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
// Ownership: create() returns the sole owner of its pipeline and mesh buffers. Copy and move
// are not provided. Lifetime: destroy it before the Device, and after any Swapchain it drew to
// (the swapchain's destructor waits for the GPU). Threading: the Device's thread.
//
// Failure: create() returns nullptr and fills error (bad SPIR-V, driver refusals). draw()
// returns false and fills error when the RHI refuses or the driver fails.
class Renderer {
public:
    // lit_vertex and lit_fragment are the SPIR-V of engine/shaders/lit.slang's vertexMain and
    // fragmentMain. color_format must match the targets the renderer draws to.
    [[nodiscard]] static std::unique_ptr<Renderer> create(
        const rhi::Device& device, rhi::ColorFormat color_format, std::span<const std::uint32_t> lit_vertex,
        std::span<const std::uint32_t> lit_fragment, rhi::RhiError& error);

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

    // Entities drawn by the last draw().
    [[nodiscard]] std::size_t last_draw_count() const;

private:
    struct Impl;
    explicit Renderer(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}
