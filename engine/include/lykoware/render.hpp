#pragma once

#include <lykoware/ecs.hpp>
#include <lykoware/image_decode.hpp>
#include <lykoware/math.hpp>
#include <lykoware/mesh.hpp>
#include <lykoware/rhi/device.hpp>
#include <lykoware/rhi/pipeline.hpp>
#include <lykoware/rhi/render_target.hpp>
#include <lykoware/rhi/shadow_map.hpp>
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

// A surface beyond its base colour (ADR 0085): metal-roughness shading with an optional normal
// map, drawn only by a renderer given material shaders (Renderer::enable_materials). The
// factors multiply the textures, as in glTF.
struct Material {
    // Microfacet roughness, 0 (mirror) .. 1 (fully rough). Multiplies metal_roughness's green.
    float roughness = 1.F;
    // 0 (dielectric) .. 1 (metal). Multiplies metal_roughness's blue.
    float metalness = 0.F;
    // How far the normal map tilts the surface, 0 (flat) .. 2.
    float normal_scale = 1.F;
    // A tangent-space normal map in a linear format (rhi::TextureFormat::Rgba8Unorm): x right and
    // y up in the image, z out of the surface. None: the surface's own normal.
    TextureId normal_map{};
    // Linear: green roughness, blue metalness. None: the factors alone.
    TextureId metal_roughness{};

    friend bool operator==(const Material&, const Material&) = default;
};

// One part of an added mesh with its own material (ADR 0038): the triangles from first_index
// for index_count indices. Its colour multiplies MeshRenderer::colour; its texture, when set,
// takes the place of MeshRenderer::texture.
struct MeshPart {
    std::uint32_t first_index = 0;
    std::uint32_t index_count = 0;
    math::Vec3 colour{1.F, 1.F, 1.F};
    TextureId texture{};
    // When set, takes the place of MeshRenderer::material for this part (ADR 0085).
    std::optional<Material> material{};
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
    // Roughness, metalness and a normal map (ADR 0085). Unset: lit as base colour alone. Drawn
    // as a material only while the renderer has material shaders; otherwise as unset.
    std::optional<Material> material{};
    // How much of what lies behind the surface it hides, 0 (none) .. 1 (opaque) (ADR 0086). Below
    // 1, and only while the renderer has transparency shaders (Renderer::enable_transparency), the
    // entity is drawn blended after every opaque one, far to near. Otherwise it is drawn opaque.
    float opacity = 1.F;
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

// The scene's one directional light and its ambient term (ADR 0082). Each surface gets
// colour * texture * (ambient + (1 - ambient) * max(0, n . direction)). The defaults are the
// first renderer's fixed light, so a renderer that is never given lighting draws as before.
struct Lighting {
    // Towards the light, in world space; any finite non-zero length (normalised in the shader).
    math::Vec3 direction{0.4F, 1.F, 0.3F};
    // The share of the colour a surface keeps facing away from the light, 0..1.
    float ambient = 0.25F;
};

// A shadow from the scene's light (ADR 0084): one orthographic map along Lighting::direction,
// a square centred on the camera's target. Off unless Renderer::enable_shadows is called.
struct Shadows {
    // The map's side in texels (rhi::shadow_map_min_size..rhi::shadow_map_max_size).
    std::uint32_t resolution = 2048;
    // Half the side of the square the map covers, metres. Outside it surfaces are lit.
    float half_extent = 16.F;
    // The map's reach along the light, metres, centred on the target. Casters beyond it cast nothing.
    float depth_range = 64.F;
    // Depth bias along the light, metres, against a surface shadowing itself (0..depth_range).
    float bias = 0.05F;
};

// SPIR-V for shadows: engine/shaders/shadow.slang vertexMain (the caster), and lit.slang
// vertexShadowed and fragmentShadowed (the receiver).
struct ShadowShaders {
    std::span<const std::uint32_t> caster_vertex;
    std::span<const std::uint32_t> receiver_vertex;
    std::span<const std::uint32_t> receiver_fragment;
};

// SPIR-V for materials (ADR 0085): engine/shaders/material.slang vertexMain and fragmentMain,
// and vertexShadowed and fragmentShadowed for when shadows are on as well.
struct MaterialShaders {
    std::span<const std::uint32_t> vertex;
    std::span<const std::uint32_t> fragment;
    std::span<const std::uint32_t> shadowed_vertex;
    std::span<const std::uint32_t> shadowed_fragment;
};

// SPIR-V for transparent surfaces (ADR 0086): engine/shaders/transparent.slang vertexMain and
// fragmentMain.
struct TransparencyShaders {
    std::span<const std::uint32_t> vertex;
    std::span<const std::uint32_t> fragment;
};

// Tone mapping after the scene (ADR 0087): the scene is drawn into a 16-bit float target, then
// each channel c becomes e c / (1 + e c) with e the exposure (Reinhard), into the final image.
inline constexpr float post_max_exposure = 64.F;
struct PostProcess {
    // Multiplies the scene's linear colour before tone mapping, above 0 .. post_max_exposure.
    float exposure = 1.F;
};

// SPIR-V for post-processing: engine/shaders/post.slang vertexMain and fragmentMain.
struct PostShaders {
    std::span<const std::uint32_t> vertex;
    std::span<const std::uint32_t> fragment;
};

// What draw() may skip and reorder (ADR 0088). Both off by default: a renderer never given
// visibility settings draws every entity in ECS order, as before.
struct Visibility {
    // Skip an entity whose mesh's bounding sphere, placed and scaled by its Transform and
    // MeshRenderer::scale, lies wholly outside the camera's view (behind the near plane or past a
    // side). A skinned entity is never culled, since its pose may move vertices outside the bounds.
    // A culled entity still casts its shadow.
    bool cull = false;
    // Draw opaque parts grouped by mesh, then base texture, then material textures (by id, so the
    // order is the same on every run), so consecutive draws share their bindings. Transparent parts
    // keep their far-to-near order.
    bool batch = false;
};

// Counts from the last draw() (ADR 0088).
struct DrawStats {
    // Entities drawn; the same as Renderer::last_draw_count().
    std::size_t entities = 0;
    // Entities skipped because Visibility::cull found them outside the view.
    std::size_t culled = 0;
    // Indexed draws issued to the target: one per part, opaque and transparent.
    std::size_t draws = 0;
    // Runs of consecutive draws sharing pipeline, mesh buffers, textures and no skin pose; the
    // RHI binds each run's state once.
    std::size_t batches = 0;
};

// What a renderer holds on the GPU for added meshes and textures (ADR 0088). The renderer's own
// built-in cube, fallback textures and pipelines are not counted.
struct GpuResources {
    std::size_t meshes = 0;
    std::size_t textures = 0;
    // Vertex and index bytes of the added meshes.
    std::size_t mesh_bytes = 0;
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
    // part needs a non-zero, whole-triangle range inside the mesh's indices, a texture id
    // this renderer made (or none), and a valid material if it has one (see enable_materials);
    // otherwise InvalidArgument.
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

    // Lighting for every draw() from now on. False, error InvalidArgument and the lighting
    // unchanged, for a direction not finite or of zero length, or an ambient outside 0..1.
    [[nodiscard]] bool set_lighting(const Lighting& lighting, rhi::RhiError& error);
    [[nodiscard]] const Lighting& lighting() const;

    // Every draw() from now on also casts and receives the light's shadow (ADR 0084). Calling it
    // again replaces the settings and shaders. False, error filled and shadows unchanged, for:
    // a renderer made with the skinned vertex shader (skinned shadows are a later slice), or a
    // resolution, extent, range or bias outside its range or not finite (InvalidArgument);
    // shaders that do not load or link (as Renderer::create).
    [[nodiscard]] bool enable_shadows(const ShadowShaders& shaders, const Shadows& shadows, rhi::RhiError& error);
    // Back to the unshadowed path, which draws exactly as if shadows were never enabled.
    void disable_shadows();
    // The settings while shadows are on, nullopt when off.
    [[nodiscard]] std::optional<Shadows> shadows() const;
    // Casters drawn into the shadow map by the last draw(); 0 when shadows are off.
    [[nodiscard]] std::size_t last_shadow_caster_count() const;

    // Every draw() from now on shades materials (ADR 0085): parts with a Material get
    // metal-roughness shading and their normal map; parts without one draw as the lit shader
    // would. Calling it again replaces the shaders. False, error filled and materials unchanged,
    // for: a renderer made with the skinned vertex shader (InvalidArgument; skinned materials
    // are a later slice), shaders that do not load or link (as Renderer::create), or a device
    // with fewer than 192 bytes of push constants (NoSuitableDevice).
    //
    // While on, draw() fails with InvalidArgument for a Material with a factor outside its range
    // or not finite, a texture id this renderer did not make, or an sRGB normal map.
    [[nodiscard]] bool enable_materials(const MaterialShaders& shaders, rhi::RhiError& error);
    // Back to drawing every part with its base colour alone, exactly as before enable_materials.
    void disable_materials();
    [[nodiscard]] bool materials_enabled() const;
    // Parts drawn with a Material by the last draw(); 0 when materials are off.
    [[nodiscard]] std::size_t last_material_draw_count() const;

    // Every draw() from now on blends entities whose MeshRenderer::opacity is below 1 (ADR 0086):
    // opaque entities first in ECS order, then transparent ones sorted far to near by the
    // distance from the camera to their Transform's position (ties keep ECS order), each part
    // lit as the lit shader would and blended over what is behind it. A transparent entity
    // tests depth and does not write it, casts no shadow, receives none and draws without its
    // Material. Calling it again replaces the shaders. False, error filled and transparency
    // unchanged, for: a renderer made with the skinned vertex shader (InvalidArgument; a later
    // slice), shaders that do not load or link (as Renderer::create), or a device with fewer
    // than 144 bytes of push constants (NoSuitableDevice).
    //
    // While on, draw() fails with InvalidArgument for an opacity outside 0..1 or not finite.
    [[nodiscard]] bool enable_transparency(const TransparencyShaders& shaders, rhi::RhiError& error);
    // Back to drawing every entity opaque, exactly as before enable_transparency.
    void disable_transparency();
    [[nodiscard]] bool transparency_enabled() const;
    // Parts drawn blended by the last draw(); 0 when transparency is off.
    [[nodiscard]] std::size_t last_transparent_draw_count() const;

    // Every draw() from now on renders the scene into a 16-bit float target and tone-maps it into
    // the final image (ADR 0087). Values above 1 survive until the tone map, so a bright surface
    // and a brighter one stay apart. Calling it again replaces the shaders and settings. The
    // scene pipelines are built again for the float target from the SPIR-V they were made with,
    // here and whenever shadows, materials or transparency are enabled later. False and error
    // filled for the cases below; post-processing is then unchanged, except that a failure to
    // build the float pipelines leaves it off: a renderer made with the skinned vertex shader
    // (InvalidArgument; a later slice), an exposure not finite, at or below 0 or above
    // post_max_exposure (InvalidArgument), shaders that do not load or link (as Renderer::create),
    // or a device that cannot render to and sample 16-bit float colour (NoSuitableDevice, at the
    // first draw).
    [[nodiscard]] bool enable_post(const PostShaders& shaders, const PostProcess& post, rhi::RhiError& error);
    // Back to drawing straight into the final image, exactly as before enable_post.
    void disable_post();
    // The settings while post-processing is on, nullopt when off.
    [[nodiscard]] std::optional<PostProcess> post() const;

    // Entities drawn by the last draw() (a mesh of several parts counts once).
    [[nodiscard]] std::size_t last_draw_count() const;

    // Culling and batching for every draw() from now on (ADR 0088). Neither changes the image of
    // a scene whose opaque surfaces do not overlap at equal depth.
    void set_visibility(const Visibility& visibility);
    [[nodiscard]] const Visibility& visibility() const;
    [[nodiscard]] const DrawStats& last_draw_stats() const;

    // Frees an added mesh or texture (ADR 0088). Waits for the device to finish its work first,
    // so a frame still in flight never reads freed memory. The id then draws nothing, as a foreign
    // id does, and is never handed out again. False, error filled and nothing freed for: an id this
    // renderer did not make or already freed (InvalidArgument), a texture a live mesh part or part
    // material still names (InvalidArgument), or a failed wait (as Device::wait_idle).
    [[nodiscard]] bool remove_mesh(MeshId id, rhi::RhiError& error);
    [[nodiscard]] bool remove_texture(TextureId id, rhi::RhiError& error);
    [[nodiscard]] GpuResources gpu_resources() const;

private:
    struct Impl;
    explicit Renderer(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}
