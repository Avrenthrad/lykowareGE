#include <lykoware/render.hpp>

#include <lykoware/rhi/buffer.hpp>
#include <lykoware/rhi/hdr_target.hpp>
#include <lykoware/rhi/shadow_map.hpp>
#include <lykoware/rhi/texture.hpp>

#include "world_identity.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace lykoware::render {

namespace {

// Matches engine/shaders/lit.slang: projection * view * model by column, the model rotation by
// column (for normals), and the colour.
struct Constants {
    math::Mat4 clip;
    math::Vec4 normal[3];
    math::Vec4 colour;
};
static_assert(sizeof(Constants) == rhi::push_constant_max_bytes);

// Matches engine/shaders/material.slang (ADR 0085): the lit block, then the camera-relative model
// (rotation * scale by column, w the translation) for the view direction, and the material
// factors: roughness, metalness, normal scale, and 1 for a part without a material.
struct MaterialConstants {
    Constants lit;
    math::Vec4 model[3];
    math::Vec4 material;
};
static_assert(sizeof(MaterialConstants) == 192);
static_assert(sizeof(MaterialConstants) <= rhi::push_constant_material_max_bytes);

// Matches engine/shaders/transparent.slang (ADR 0086): the lit block, then x the opacity.
struct TransparentConstants {
    Constants lit;
    math::Vec4 surface;
};
static_assert(sizeof(TransparentConstants) == 144);
static_assert(sizeof(TransparentConstants) <= rhi::push_constant_material_max_bytes);

// A pipeline's SPIR-V and description, kept so the renderer can build it again for another
// colour format: the HDR target of the post pass (ADR 0087).
struct Recipe {
    std::vector<std::uint32_t> vertex;
    std::vector<std::uint32_t> fragment;
    rhi::GraphicsPipelineDesc desc;
};

Recipe recipe(std::span<const std::uint32_t> vertex, std::span<const std::uint32_t> fragment,
              const rhi::GraphicsPipelineDesc& desc)
{
    Recipe made{{vertex.begin(), vertex.end()}, {fragment.begin(), fragment.end()}, desc};
    made.desc.vertex = nullptr;
    made.desc.fragment = nullptr;
    return made;
}

std::unique_ptr<rhi::GraphicsPipeline> build(const rhi::Device& device, const Recipe& made, rhi::ColorFormat format,
                                             rhi::RhiError& error)
{
    const auto vertex = rhi::Shader::load(device, made.vertex, rhi::ShaderStage::Vertex, error);
    const auto fragment = vertex ? rhi::Shader::load(device, made.fragment, rhi::ShaderStage::Fragment, error) : nullptr;
    if (!vertex || !fragment) {
        return nullptr;
    }
    rhi::GraphicsPipelineDesc desc = made.desc;
    desc.vertex = vertex.get();
    desc.fragment = fragment.get();
    desc.color_format = format;
    return rhi::GraphicsPipeline::create(device, desc, error);
}

bool valid_factors(const Material& material)
{
    const auto in = [](float value, float high) { return std::isfinite(value) && value >= 0.F && value <= high; };
    return in(material.roughness, 1.F) && in(material.metalness, 1.F) && in(material.normal_scale, 2.F);
}

struct Vertex {
    math::Vec3 position;
    math::Vec3 normal;
    math::Vec2 uv;
};
static_assert(sizeof(Vertex) == rhi::position_normal_uv_stride);
struct SkinVertex {
    Vertex base;
    std::array<std::uint16_t, 4> joints{};
    math::Vec4 weights{}; // static geometry takes the shader's zero-weight fast path
};
static_assert(sizeof(SkinVertex) == rhi::skinned_position_normal_uv_stride);
static_assert(offsetof(SkinVertex, joints) == 32);
static_assert(offsetof(SkinVertex, weights) == 40);

// A unit cube centred on the origin: four vertices per face with the face normal, two
// triangles per face, counter-clockwise when seen from outside (u x v = n for each face). Each
// face shows the whole texture, upright when the face's v axis points up.
void build_cube(std::vector<Vertex>& vertices, std::vector<std::uint32_t>& indices)
{
    struct Face {
        math::Vec3 n;
        math::Vec3 u;
        math::Vec3 v;
    };
    constexpr std::array<Face, 6> faces{{
        {{1.F, 0.F, 0.F}, {0.F, 0.F, -1.F}, {0.F, 1.F, 0.F}},
        {{-1.F, 0.F, 0.F}, {0.F, 0.F, 1.F}, {0.F, 1.F, 0.F}},
        {{0.F, 1.F, 0.F}, {1.F, 0.F, 0.F}, {0.F, 0.F, -1.F}},
        {{0.F, -1.F, 0.F}, {1.F, 0.F, 0.F}, {0.F, 0.F, 1.F}},
        {{0.F, 0.F, 1.F}, {1.F, 0.F, 0.F}, {0.F, 1.F, 0.F}},
        {{0.F, 0.F, -1.F}, {-1.F, 0.F, 0.F}, {0.F, 1.F, 0.F}},
    }};
    constexpr std::array<std::pair<float, float>, 4> corners{{{-0.5F, -0.5F}, {0.5F, -0.5F}, {0.5F, 0.5F}, {-0.5F, 0.5F}}};
    for (const Face& face : faces) {
        const auto base = static_cast<std::uint32_t>(vertices.size());
        for (const auto& [su, sv] : corners) {
            vertices.push_back({face.n * 0.5F + face.u * su + face.v * sv, face.n, {su + 0.5F, 0.5F - sv}});
        }
        for (const std::uint32_t corner : {0U, 1U, 2U, 0U, 2U, 3U}) {
            indices.push_back(base + corner);
        }
    }
}

struct GpuMesh {
    std::unique_ptr<rhi::Buffer> vertices;
    std::unique_ptr<rhi::Buffer> indices;
    std::vector<MeshPart> parts;
    std::size_t joint_count = 0;
};

// One draw: a part of a mesh with the texture it uses.
struct Drawn {
    const GpuMesh* mesh = nullptr;
    const rhi::Texture* texture = nullptr;
    std::uint32_t first_index = 0;
    std::uint32_t index_count = 0;
    std::span<const math::Mat4> skin{};
    // Material draws only: set 2 and set 3.
    const rhi::Texture* normal_map = nullptr;
    const rhi::Texture* metal_roughness = nullptr;
};

// The inverse of math::perspective(vertical_fov, aspect, near), given f = 1 / tan(fov / 2) and
// a = f / aspect: that maps (x, y, z, w) to (a x, -f y, near w, -z).
math::Mat4 inverse_perspective(float a, float f, float near)
{
    math::Mat4 r;
    r.m[0][0] = 1.F / a;
    r.m[1][1] = -1.F / f;
    r.m[2][2] = 0.F;
    r.m[2][3] = 1.F / near;
    r.m[3][2] = -1.F;
    r.m[3][3] = 0.F;
    return r;
}

// The shadow's view and projection (ADR 0084), camera-relative: an orthographic square of
// half side `half_extent` around `focus`, looking along -towards, `depth_range` deep and centred
// on the focus. Reversed depth as everywhere: 1 at the near plane (towards the light), 0 at the far.
math::Mat4 light_view_projection(math::Vec3 focus, math::Vec3 towards, float half_extent, float depth_range)
{
    const math::Vec3 l = math::normalize(towards);
    const math::Vec3 up = std::abs(l.y) > 0.99F ? math::Vec3{0.F, 0.F, 1.F} : math::Vec3{0.F, 1.F, 0.F};
    const math::Mat4 view = math::look_at(focus + l * (depth_range * 0.5F), focus, up);
    math::Mat4 projection;
    projection.m[0][0] = 1.F / half_extent;
    projection.m[1][1] = -1.F / half_extent;
    projection.m[2][2] = 1.F / depth_range;
    projection.m[3][2] = 1.F;
    return projection * view;
}

template <typename T>
bool upload(const rhi::Device& device, std::span<const T> vertices, std::span<const std::uint32_t> indices,
            GpuMesh& mesh, rhi::RhiError& error)
{
    mesh.vertices = rhi::Buffer::create(device, rhi::BufferUsage::Vertex, std::as_bytes(vertices), error);
    mesh.indices = mesh.vertices ? rhi::Buffer::create(device, rhi::BufferUsage::Index, std::as_bytes(indices), error) : nullptr;
    return mesh.vertices && mesh.indices;
}

}

struct Renderer::Impl {
    const rhi::Device* device = nullptr;
    // This renderer's identity; its ids carry it (ADR 0030).
    std::uint64_t owner = 0;
    std::unique_ptr<rhi::GraphicsPipeline> pipeline;
    bool skinning = false;
    GpuMesh cube;
    // Added meshes; MeshId::value is the position here plus one.
    std::vector<GpuMesh> meshes;
    // A 1x1 white texture for entities without one, and added textures (TextureId::value is the
    // position here plus one).
    std::unique_ptr<rhi::Texture> white;
    std::vector<std::unique_ptr<rhi::Texture>> textures;
    std::vector<Constants> constants;
    std::vector<Drawn> drawn;
    std::vector<rhi::MeshDraw> draws;
    std::size_t entities_drawn = 0;
    Lighting lighting;
    // Shadows (ADR 0084): all null while off.
    std::optional<Shadows> shadow_settings;
    std::unique_ptr<rhi::GraphicsPipeline> caster_pipeline;
    std::unique_ptr<rhi::GraphicsPipeline> receiver_pipeline;
    std::unique_ptr<rhi::ShadowMap> shadow_map;
    std::vector<math::Mat4> caster_constants;
    std::vector<rhi::MeshDraw> caster_draws;
    std::vector<const GpuMesh*> caster_meshes;
    rhi::ShadowBlock shadow_block;
    std::size_t casters_drawn = 0;
    // Materials (ADR 0085): null while off. `flat` is the 1x1 linear normal map for parts
    // without one; the white texture stands in for a missing metal-roughness texture.
    std::unique_ptr<rhi::GraphicsPipeline> material_pipeline;
    std::unique_ptr<rhi::GraphicsPipeline> material_shadowed_pipeline;
    std::unique_ptr<rhi::Texture> flat;
    // On between enable_materials and disable_materials; the pipelines stay alive after a
    // disable, since a swapchain frame may still be drawing with them.
    bool materials_on = false;
    std::vector<MaterialConstants> material_constants;
    std::size_t materials_drawn = 0;
    // Transparency (ADR 0086): the pipeline stays alive after a disable, as materials' do.
    std::unique_ptr<rhi::GraphicsPipeline> transparent_pipeline;
    bool transparency_on = false;
    std::vector<TransparentConstants> transparent_constants;
    // Each transparent part and the distance from the camera to its entity, in ECS order.
    std::vector<std::pair<Drawn, double>> transparent_parts;
    std::vector<std::size_t> transparent_order;
    std::size_t transparent_drawn = 0;
    // Post-processing (ADR 0087). The recipes of every scene pipeline made so far; `version`
    // changes whenever one is replaced, and the HDR copies are rebuilt when they are older.
    Recipe lit_recipe;
    Recipe receiver_recipe;
    Recipe material_recipes[2];
    Recipe transparent_recipe;
    std::uint64_t recipes_version = 1;
    std::uint64_t hdr_version = 0;
    std::unique_ptr<rhi::GraphicsPipeline> hdr_lit;
    std::unique_ptr<rhi::GraphicsPipeline> hdr_receiver;
    std::unique_ptr<rhi::GraphicsPipeline> hdr_material[2];
    std::unique_ptr<rhi::GraphicsPipeline> hdr_transparent;
    std::unique_ptr<rhi::GraphicsPipeline> post_pipeline;
    std::unique_ptr<rhi::HdrTarget> hdr_target;
    GpuMesh fullscreen;
    std::optional<PostProcess> post_settings;
    math::Vec4 post_constants{};
    rhi::MeshDraw post_draw;

    // Builds the HDR copies of the scene pipelines when a recipe changed since the last build.
    bool ensure_hdr(rhi::RhiError& error)
    {
        if (hdr_version == recipes_version) {
            return true;
        }
        const auto copy = [&](const Recipe& made, std::unique_ptr<rhi::GraphicsPipeline>& into) {
            into.reset();
            if (made.vertex.empty()) {
                return true;
            }
            into = build(*device, made, rhi::ColorFormat::Rgba16Float, error);
            return into != nullptr;
        };
        if (!copy(lit_recipe, hdr_lit) || !copy(receiver_recipe, hdr_receiver)
            || !copy(material_recipes[0], hdr_material[0]) || !copy(material_recipes[1], hdr_material[1])
            || !copy(transparent_recipe, hdr_transparent)) {
            return false;
        }
        hdr_version = recipes_version;
        return true;
    }

    // Draws the prepared scene into the HDR target, sized to the final image, then fills
    // `post_draw` for the tone-mapping pass.
    bool render_hdr(std::uint32_t width, std::uint32_t height, const rhi::ClearColor& clear, rhi::RhiError& error)
    {
        if (!hdr_target || hdr_target->width() != width || hdr_target->height() != height) {
            hdr_target.reset();
            hdr_target = rhi::HdrTarget::create(*device, width, height, error);
            if (!hdr_target) {
                return false;
            }
        }
        if (!hdr_target->render(clear, active_pipeline(), draws, error)) {
            return false;
        }
        post_constants = {post_settings->exposure, 0.F, 0.F, 0.F};
        post_draw = {};
        post_draw.vertices = fullscreen.vertices.get();
        post_draw.indices = fullscreen.indices.get();
        post_draw.push_constants = std::as_bytes(std::span(&post_constants, 1));
        post_draw.source = hdr_target.get();
        return true;
    }

    // The texture an id names, `fallback` for none, or null for an id this renderer did not make.
    const rhi::Texture* texture_or(TextureId id, const rhi::Texture* fallback) const
    {
        if (id == TextureId{}) {
            return fallback;
        }
        if (id.value == 0 || id.owner != owner || id.value > textures.size()) {
            return nullptr;
        }
        return textures[id.value - 1].get();
    }

    // Empty when the material can be drawn, else why not.
    std::string material_problem(const Material& material) const
    {
        if (!valid_factors(material)) {
            return "material roughness and metalness must be 0..1 and normal_scale 0..2, all finite";
        }
        const rhi::Texture* normal = texture_or(material.normal_map, flat.get());
        if (normal == nullptr || texture_or(material.metal_roughness, white.get()) == nullptr) {
            return "a material names a texture this renderer did not make";
        }
        if (normal->format() != rhi::TextureFormat::Rgba8Unorm) {
            return "a normal map must be linear (Rgba8Unorm), not sRGB";
        }
        return {};
    }

    const rhi::Texture* texture_for(const MeshRenderer& renderer) const
    {
        if (renderer.texture == TextureId{}) {
            return white.get();
        }
        if (renderer.texture.value == 0 || renderer.texture.owner != owner || renderer.texture.value > textures.size()) {
            return nullptr;
        }
        return textures[renderer.texture.value - 1].get();
    }

    const GpuMesh* mesh_for(const MeshRenderer& renderer) const
    {
        if (renderer.mesh == MeshId{}) {
            return &cube;
        }
        if (renderer.mesh.value == 0 || renderer.mesh.owner != owner || renderer.mesh.value > meshes.size()) {
            return nullptr;
        }
        return &meshes[renderer.mesh.value - 1];
    }

    const rhi::GraphicsPipeline& active_pipeline() const
    {
        if (post_settings) {
            if (materials_on) {
                return shadow_settings ? *hdr_material[1] : *hdr_material[0];
            }
            return shadow_settings ? *hdr_receiver : *hdr_lit;
        }
        if (materials_on) {
            return shadow_settings ? *material_shadowed_pipeline : *material_pipeline;
        }
        return shadow_settings ? *receiver_pipeline : *pipeline;
    }

    // Draws the shadow map for the draws prepare() built; nothing while shadows are off.
    bool cast_shadows(rhi::RhiError& error)
    {
        casters_drawn = 0;
        if (!shadow_settings) {
            return true;
        }
        if (!shadow_map->render(*caster_pipeline, caster_draws, shadow_block, error)) {
            return false;
        }
        casters_drawn = shadow_map->last_caster_count();
        return true;
    }

    // Builds one draw per entity with Transform and MeshRenderer, relative to the camera.
    bool prepare(const ecs::World& entities, const Camera& camera, float aspect, rhi::RhiError& error)
    {
        bool valid = true;
        error = {};
        constants.clear();
        material_constants.clear();
        transparent_constants.clear();
        transparent_parts.clear();
        transparent_drawn = 0;
        drawn.clear();
        draws.clear();
        entities_drawn = 0;
        materials_drawn = 0;
        const math::Mat4 view =
            math::look_at({0.F, 0.F, 0.F}, math::relative_to(camera.target, camera.eye), camera.up);
        const math::Mat4 projection = math::perspective(camera.vertical_fov, aspect, camera.near);
        const math::Mat4 view_projection = projection * view;
        caster_constants.clear();
        caster_draws.clear();
        math::Mat4 light_projection;
        if (shadow_settings) {
            const Shadows& settings = *shadow_settings;
            light_projection = light_view_projection(math::relative_to(camera.target, camera.eye), lighting.direction,
                                                     settings.half_extent, settings.depth_range);
            const float f = 1.F / std::tan(camera.vertical_fov * 0.5F);
            shadow_block.clip_to_light =
                light_projection * math::rigid_inverse(view) * inverse_perspective(f / aspect, f, camera.near);
            shadow_block.params = {settings.bias / settings.depth_range,
                                   1.F / static_cast<float>(settings.resolution), 0.F, 0.F};
        }
        entities.each<Transform, MeshRenderer>([&](ecs::Entity entity, const Transform& transform, const MeshRenderer& mesh) {
            const GpuMesh* gpu = mesh_for(mesh);
            const rhi::Texture* texture = texture_for(mesh);
            if (gpu == nullptr || texture == nullptr) {
                return; // a mesh or texture id this renderer did not make
            }
            std::span<const math::Mat4> skin;
            if (gpu->joint_count > 0) {
                if (const auto* pose = entities.get<SkinPose>(entity)) {
                    if (pose->matrices.size() != gpu->joint_count) {
                        error = {rhi::RhiFailure::InvalidArgument, "SkinPose count differs from the mesh's joint count"};
                        valid = false;
                        return;
                    }
                    skin = pose->matrices;
                }
            }
            if (transparency_on && !(std::isfinite(mesh.opacity) && mesh.opacity >= 0.F && mesh.opacity <= 1.F)) {
                error = {rhi::RhiFailure::InvalidArgument, "MeshRenderer::opacity must be finite and 0..1"};
                valid = false;
                return;
            }
            // Below 1 while transparency is on: blended after the opaque draws, and no caster.
            const bool transparent = transparency_on && mesh.opacity < 1.F;
            const math::Mat4 model =
                math::trs(math::relative_to(transform.position, camera.eye), transform.rotation, mesh.scale);
            if (shadow_settings && !transparent) {
                caster_constants.push_back(light_projection * model);
                caster_meshes.push_back(gpu);
            }
            Constants c;
            c.clip = view_projection * model;
            // Normals go through the inverse transpose of rotation * scale, which is rotation *
            // (1 / scale); the shader makes them unit length. Uneven scale then still lights
            // slanted faces correctly.
            const math::Mat4 rotation = math::rotation(transform.rotation);
            const float inverse[3] = {mesh.scale.x != 0.F ? 1.F / mesh.scale.x : 0.F,
                                      mesh.scale.y != 0.F ? 1.F / mesh.scale.y : 0.F,
                                      mesh.scale.z != 0.F ? 1.F / mesh.scale.z : 0.F};
            for (int column = 0; column < 3; ++column) {
                const float s = inverse[column];
                // w carries the light's direction (ADR 0082); the vertex shader reads xyz only.
                const float towards[3] = {lighting.direction.x, lighting.direction.y, lighting.direction.z};
                c.normal[column] = {rotation.m[column][0] * s, rotation.m[column][1] * s, rotation.m[column][2] * s,
                                    towards[column]};
            }
            for (const MeshPart& part : gpu->parts) {
                c.colour = {mesh.colour.x * part.colour.x, mesh.colour.y * part.colour.y, mesh.colour.z * part.colour.z, lighting.ambient};
                const rhi::Texture* part_texture =
                    part.texture == TextureId{} ? texture : textures[part.texture.value - 1].get();
                if (transparent) {
                    const double dx = transform.position.x - camera.eye.x;
                    const double dy = transform.position.y - camera.eye.y;
                    const double dz = transform.position.z - camera.eye.z;
                    transparent_constants.push_back({c, {mesh.opacity, 0.F, 0.F, 0.F}});
                    transparent_parts.push_back({Drawn{gpu, part_texture, part.first_index, part.index_count, skin},
                                                 std::sqrt(dx * dx + dy * dy + dz * dz)});
                    continue;
                }
                if (!materials_on) {
                    constants.push_back(c);
                    drawn.push_back({gpu, part_texture, part.first_index, part.index_count, skin});
                    continue;
                }
                MaterialConstants m;
                m.lit = c;
                for (int column = 0; column < 3; ++column) {
                    m.model[column] = {model.m[column][0], model.m[column][1], model.m[column][2], model.m[3][column]};
                }
                const std::optional<Material>& chosen = part.material ? part.material : mesh.material;
                Drawn d{gpu, part_texture, part.first_index, part.index_count, skin, flat.get(), white.get()};
                if (chosen) {
                    if (const std::string problem = material_problem(*chosen); !problem.empty()) {
                        error = {rhi::RhiFailure::InvalidArgument, problem};
                        valid = false;
                        return;
                    }
                    // No normal map: a scale of 0 keeps the surface's own normal exactly.
                    const float scale = chosen->normal_map == TextureId{} ? 0.F : chosen->normal_scale;
                    m.material = {chosen->roughness, chosen->metalness, scale, 0.F};
                    d.normal_map = texture_or(chosen->normal_map, flat.get());
                    d.metal_roughness = texture_or(chosen->metal_roughness, white.get());
                    ++materials_drawn;
                } else {
                    m.material = {1.F, 0.F, 0.F, 1.F};
                }
                material_constants.push_back(m);
                drawn.push_back(d);
            }
            ++entities_drawn;
        });
        // Push-constant spans point into `constants`, so build them after it stops growing.
        for (std::size_t i = 0; i < drawn.size(); ++i) {
            const auto bytes = materials_on ? std::as_bytes(std::span(&material_constants[i], 1))
                                                 : std::as_bytes(std::span(&constants[i], 1));
            draws.push_back({drawn[i].mesh->vertices.get(), drawn[i].mesh->indices.get(), drawn[i].index_count,
                             bytes, drawn[i].texture, drawn[i].first_index, drawn[i].skin});
            draws.back().shadow = shadow_settings ? shadow_map.get() : nullptr;
            draws.back().normal_map = drawn[i].normal_map;
            draws.back().metal_roughness = drawn[i].metal_roughness;
        }
        // Transparent parts last, far to near; a stable sort keeps ECS and part order on ties.
        transparent_order.resize(transparent_parts.size());
        for (std::size_t i = 0; i < transparent_order.size(); ++i) {
            transparent_order[i] = i;
        }
        std::stable_sort(transparent_order.begin(), transparent_order.end(), [&](std::size_t a, std::size_t b) {
            return transparent_parts[a].second > transparent_parts[b].second;
        });
        for (const std::size_t i : transparent_order) {
            const Drawn& part = transparent_parts[i].first;
            draws.push_back({part.mesh->vertices.get(), part.mesh->indices.get(), part.index_count,
                             std::as_bytes(std::span(&transparent_constants[i], 1)), part.texture, part.first_index,
                             part.skin});
            draws.back().pipeline = post_settings ? hdr_transparent.get() : transparent_pipeline.get();
        }
        transparent_drawn = transparent_parts.size();
        // Casters draw every index of their mesh (all parts at once), from the light.
        for (std::size_t i = 0; i < caster_constants.size(); ++i) {
            rhi::MeshDraw caster;
            caster.vertices = caster_meshes[i]->vertices.get();
            caster.indices = caster_meshes[i]->indices.get();
            caster.push_constants = std::as_bytes(std::span(&caster_constants[i], 1));
            caster_draws.push_back(caster);
        }
        caster_meshes.clear();
        return valid;
    }
};

Renderer::Renderer(std::unique_ptr<Impl> impl)
    : impl_{std::move(impl)}
{
}

Renderer::~Renderer() = default;

std::unique_ptr<Renderer> Renderer::create(const rhi::Device& device, rhi::ColorFormat color_format,
                                           std::span<const std::uint32_t> lit_vertex,
                                           std::span<const std::uint32_t> lit_fragment, rhi::RhiError& error,
                                           std::span<const std::uint32_t> skinned_vertex)
{
    auto impl = std::make_unique<Impl>();
    impl->device = &device;
    impl->owner = detail::next_world_identity();
    impl->skinning = !skinned_vertex.empty();
    {
        const auto vertex = rhi::Shader::load(device, impl->skinning ? skinned_vertex : lit_vertex, rhi::ShaderStage::Vertex, error);
        const auto fragment = vertex ? rhi::Shader::load(device, lit_fragment, rhi::ShaderStage::Fragment, error) : nullptr;
        if (!vertex || !fragment) {
            return nullptr;
        }
        rhi::GraphicsPipelineDesc desc;
        desc.vertex = vertex.get();
        desc.fragment = fragment.get();
        desc.color_format = color_format;
        desc.vertex_layout = impl->skinning ? rhi::VertexLayout::SkinnedPositionNormalUv : rhi::VertexLayout::PositionNormalUv;
        desc.depth = true;
        desc.cull_back_faces = true;
        desc.push_constant_bytes = rhi::push_constant_max_bytes;
        desc.texture = true;
        impl->pipeline = rhi::GraphicsPipeline::create(device, desc, error);
        if (!impl->pipeline) {
            return nullptr;
        }
        impl->lit_recipe = recipe(impl->skinning ? skinned_vertex : lit_vertex, lit_fragment, desc);
    }
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    build_cube(vertices, indices);
    bool uploaded = false;
    if (impl->skinning) {
        std::vector<SkinVertex> skin_vertices;
        for (const auto& vertex : vertices) { skin_vertices.push_back({vertex}); }
        uploaded = upload(device, std::span<const SkinVertex>(skin_vertices), indices, impl->cube, error);
    } else {
        uploaded = upload(device, std::span<const Vertex>(vertices), indices, impl->cube, error);
    }
    if (!uploaded) { return nullptr; }
    impl->cube.parts.push_back({0, static_cast<std::uint32_t>(indices.size())});
    rhi::TextureDesc white;
    white.width = 1;
    white.height = 1;
    white.mipmaps = false;
    const std::byte texel[4] = {std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}};
    impl->white = rhi::Texture::create(device, white, texel, error);
    if (!impl->white) {
        return nullptr;
    }
    // The flat normal map (ADR 0085). Its texel is not exactly (0, 0, 1) after unpacking, so a part
    // without a normal map also gets a normal scale of 0; this texture only fills set 2.
    rhi::TextureDesc flat = white;
    flat.format = rhi::TextureFormat::Rgba8Unorm;
    const std::byte up[4] = {std::byte{128}, std::byte{128}, std::byte{255}, std::byte{255}};
    impl->flat = rhi::Texture::create(device, flat, up, error);
    if (!impl->flat) {
        return nullptr;
    }
    return std::unique_ptr<Renderer>(new Renderer(std::move(impl)));
}

bool Renderer::draw(const ecs::World& entities, const Camera& camera, rhi::RenderTarget& target,
                    const rhi::ClearColor& clear, rhi::RhiError& error)
{
    error = {};
    if (impl_->post_settings && !impl_->ensure_hdr(error)) { return false; }
    if (!impl_->prepare(entities, camera, static_cast<float>(target.width()) / static_cast<float>(target.height()), error)) { return false; }
    if (!impl_->cast_shadows(error)) { return false; }
    if (impl_->post_settings) {
        return impl_->render_hdr(target.width(), target.height(), clear, error)
            && target.draw_meshes(clear, *impl_->post_pipeline, std::span(&impl_->post_draw, 1), error);
    }
    return target.draw_meshes(clear, impl_->active_pipeline(), impl_->draws, error);
}

bool Renderer::draw(const ecs::World& entities, const Camera& camera, rhi::Swapchain& swapchain,
                    const rhi::ClearColor& clear, rhi::RhiError& error)
{
    const float aspect = swapchain.width() > 0 && swapchain.height() > 0
        ? static_cast<float>(swapchain.width()) / static_cast<float>(swapchain.height())
        : 1.F;
    error = {};
    if (impl_->post_settings && !impl_->ensure_hdr(error)) { return false; }
    if (!impl_->prepare(entities, camera, aspect, error)) { return false; }
    if (!impl_->cast_shadows(error)) { return false; }
    if (impl_->post_settings && swapchain.width() > 0 && swapchain.height() > 0) {
        return impl_->render_hdr(swapchain.width(), swapchain.height(), clear, error)
            && swapchain.render_meshes(clear, *impl_->post_pipeline, std::span(&impl_->post_draw, 1), error);
    }
    return swapchain.render_meshes(clear, impl_->active_pipeline(), impl_->draws, error);
}

std::optional<MeshId> Renderer::add_mesh(const MeshData& mesh, rhi::RhiError& error)
{
    const MeshPart whole{0, static_cast<std::uint32_t>(mesh.indices.size())};
    return add_mesh(mesh, std::span(&whole, 1), error);
}

std::optional<MeshId> Renderer::add_mesh(const MeshData& mesh, std::span<const MeshPart> parts, rhi::RhiError& error)
{
    std::string reason;
    if (!valid_mesh(mesh, reason)) {
        error = {rhi::RhiFailure::InvalidArgument, reason};
        return std::nullopt;
    }
    if (parts.empty()) {
        error = {rhi::RhiFailure::InvalidArgument, "a mesh needs at least one part"};
        return std::nullopt;
    }
    for (const MeshPart& part : parts) {
        const std::size_t end = static_cast<std::size_t>(part.first_index) + part.index_count;
        if (part.index_count == 0 || part.index_count % 3 != 0 || end > mesh.indices.size()) {
            error = {rhi::RhiFailure::InvalidArgument, "a mesh part must be whole triangles inside the mesh's indices"};
            return std::nullopt;
        }
        if (!(part.texture == TextureId{})
            && (part.texture.value == 0 || part.texture.owner != impl_->owner || part.texture.value > impl_->textures.size())) {
            error = {rhi::RhiFailure::InvalidArgument, "a mesh part names a texture this renderer did not make"};
            return std::nullopt;
        }
        if (part.material) {
            if (const std::string problem = impl_->material_problem(*part.material); !problem.empty()) {
                error = {rhi::RhiFailure::InvalidArgument, problem};
                return std::nullopt;
            }
        }
    }
    const std::size_t stride = impl_->skinning ? sizeof(SkinVertex) : sizeof(Vertex);
    if (mesh.positions.size() > rhi::buffer_max_bytes / stride) {
        error = {rhi::RhiFailure::InvalidArgument, "mesh vertex data exceeds the buffer limit"};
        return std::nullopt;
    }
    std::vector<Vertex> vertices(mesh.positions.size());
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        vertices[i] = {mesh.positions[i], mesh.normals[i], mesh.uvs.empty() ? math::Vec2{} : mesh.uvs[i]};
    }
    GpuMesh gpu;
    bool uploaded = false;
    if (impl_->skinning) {
        std::vector<SkinVertex> skin_vertices;
        skin_vertices.reserve(vertices.size());
        for (const auto& vertex : vertices) { skin_vertices.push_back({vertex}); }
        uploaded = upload(*impl_->device, std::span<const SkinVertex>(skin_vertices), mesh.indices, gpu, error);
    } else {
        uploaded = upload(*impl_->device, std::span<const Vertex>(vertices), mesh.indices, gpu, error);
    }
    if (!uploaded) { return std::nullopt; }
    gpu.parts.assign(parts.begin(), parts.end());
    impl_->meshes.push_back(std::move(gpu));
    return MeshId{static_cast<std::uint32_t>(impl_->meshes.size()), impl_->owner};
}

std::optional<MeshId> Renderer::add_skinned_mesh(
    const MeshData& mesh, std::span<const std::array<std::uint16_t, 4>> joints,
    std::span<const math::Vec4> weights, std::size_t joint_count,
    std::span<const MeshPart> parts, rhi::RhiError& error)
{
    error = {};
    std::string reason;
    if (!impl_->skinning || !valid_mesh(mesh, reason) || joint_count == 0 || joint_count > rhi::skin_palette_max_joints
        || joints.size() != mesh.positions.size() || weights.size() != mesh.positions.size()
        || mesh.positions.size() > rhi::buffer_max_bytes / sizeof(SkinVertex)) {
        error = {rhi::RhiFailure::InvalidArgument, "invalid skinned mesh or renderer: " + reason};
        return std::nullopt;
    }
    const MeshPart whole{0, static_cast<std::uint32_t>(mesh.indices.size())};
    if (parts.empty()) { parts = std::span(&whole, 1); }
    for (const auto& part : parts) {
        const std::size_t end = static_cast<std::size_t>(part.first_index) + part.index_count;
        if (part.index_count == 0 || part.index_count % 3 != 0 || end > mesh.indices.size()
            || (!(part.texture == TextureId{}) && (part.texture.value == 0 || part.texture.owner != impl_->owner
                                                   || part.texture.value > impl_->textures.size()))) {
            error = {rhi::RhiFailure::InvalidArgument, "invalid skinned mesh part or foreign texture"};
            return std::nullopt;
        }
        if (part.material && !impl_->material_problem(*part.material).empty()) {
            error = {rhi::RhiFailure::InvalidArgument, impl_->material_problem(*part.material)};
            return std::nullopt;
        }
    }
    std::vector<SkinVertex> vertices(mesh.positions.size());
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        const auto w = weights[i];
        const double values[] = {w.x, w.y, w.z, w.w};
        double sum = 0.0;
        for (std::size_t slot = 0; slot < 4; ++slot) {
            if (!std::isfinite(values[slot]) || values[slot] < 0.0
                || (values[slot] > 0.0 && joints[i][slot] >= joint_count)) {
                error = {rhi::RhiFailure::InvalidArgument, "invalid skin weight or joint index"};
                return std::nullopt;
            }
            sum += values[slot];
        }
        if (std::abs(sum - 1.0) > 1e-4) {
            error = {rhi::RhiFailure::InvalidArgument, "skin weights must sum to 1 within 1e-4"};
            return std::nullopt;
        }
        auto& vertex = vertices[i];
        vertex.base = {mesh.positions[i], mesh.normals[i], mesh.uvs.empty() ? math::Vec2{} : mesh.uvs[i]};
        vertex.joints = joints[i];
        // Unused indices need not exist in the source skin. Make their GPU fetch safe.
        for (std::size_t slot = 0; slot < 4; ++slot) {
            if (values[slot] == 0.0) { vertex.joints[slot] = 0; }
        }
        vertex.weights = {static_cast<float>(values[0] / sum), static_cast<float>(values[1] / sum),
                          static_cast<float>(values[2] / sum), static_cast<float>(values[3] / sum)};
    }
    GpuMesh gpu;
    if (!upload(*impl_->device, std::span<const SkinVertex>(vertices), mesh.indices, gpu, error)) { return std::nullopt; }
    gpu.parts.assign(parts.begin(), parts.end());
    gpu.joint_count = joint_count;
    impl_->meshes.push_back(std::move(gpu));
    return MeshId{static_cast<std::uint32_t>(impl_->meshes.size()), impl_->owner};
}

std::optional<TextureId> Renderer::add_texture(const RgbaImage& image, const TextureSettings& settings,
                                               rhi::RhiError& error)
{
    rhi::TextureDesc desc;
    desc.width = image.width;
    desc.height = image.height;
    desc.format = settings.format;
    desc.wrap = settings.wrap;
    desc.filter = settings.filter;
    desc.mipmaps = settings.mipmaps;
    auto texture = rhi::Texture::create(*impl_->device, desc, std::as_bytes(std::span(image.pixels)), error);
    if (!texture) {
        return std::nullopt;
    }
    impl_->textures.push_back(std::move(texture));
    return TextureId{static_cast<std::uint32_t>(impl_->textures.size()), impl_->owner};
}

std::size_t Renderer::last_draw_count() const
{
    return impl_->entities_drawn;
}

bool Renderer::set_lighting(const Lighting& lighting, rhi::RhiError& error)
{
    const math::Vec3 d = lighting.direction;
    const bool finite = std::isfinite(d.x) && std::isfinite(d.y) && std::isfinite(d.z) && std::isfinite(lighting.ambient);
    if (!finite || (d.x == 0.F && d.y == 0.F && d.z == 0.F) || lighting.ambient < 0.F || lighting.ambient > 1.F) {
        error = {rhi::RhiFailure::InvalidArgument, "Lighting needs a finite non-zero direction and an ambient in 0..1"};
        return false;
    }
    impl_->lighting = lighting;
    error = {};
    return true;
}

const Lighting& Renderer::lighting() const { return impl_->lighting; }

bool Renderer::enable_shadows(const ShadowShaders& shaders, const Shadows& shadows, rhi::RhiError& error)
{
    error = {};
    if (impl_->skinning) {
        error = {rhi::RhiFailure::InvalidArgument, "shadows are not yet drawn by a skinned renderer"};
        return false;
    }
    const bool finite = std::isfinite(shadows.half_extent) && std::isfinite(shadows.depth_range)
        && std::isfinite(shadows.bias);
    if (!finite || shadows.half_extent <= 0.F || shadows.depth_range <= 0.F || shadows.bias < 0.F
        || shadows.bias >= shadows.depth_range) {
        error = {rhi::RhiFailure::InvalidArgument,
                 "Shadows needs a positive extent and range, and a bias of 0 up to the range"};
        return false;
    }
    const rhi::Device& device = *impl_->device;
    auto map = rhi::ShadowMap::create(device, shadows.resolution, error);
    if (!map) {
        return false;
    }
    const auto caster_vertex = rhi::Shader::load(device, shaders.caster_vertex, rhi::ShaderStage::Vertex, error);
    if (!caster_vertex) {
        return false;
    }
    const auto receiver_vertex = rhi::Shader::load(device, shaders.receiver_vertex, rhi::ShaderStage::Vertex, error);
    if (!receiver_vertex) {
        return false;
    }
    const auto receiver_fragment =
        rhi::Shader::load(device, shaders.receiver_fragment, rhi::ShaderStage::Fragment, error);
    if (!receiver_fragment) {
        return false;
    }
    rhi::GraphicsPipelineDesc caster;
    caster.vertex = caster_vertex.get();
    caster.color_format = impl_->pipeline->color_format();
    caster.vertex_layout = rhi::VertexLayout::PositionNormalUv;
    caster.depth = true;
    caster.depth_only = true;
    caster.push_constant_bytes = sizeof(math::Mat4);
    auto caster_pipeline = rhi::GraphicsPipeline::create(device, caster, error);
    if (!caster_pipeline) {
        return false;
    }
    rhi::GraphicsPipelineDesc receiver;
    receiver.vertex = receiver_vertex.get();
    receiver.fragment = receiver_fragment.get();
    receiver.color_format = impl_->pipeline->color_format();
    receiver.vertex_layout = rhi::VertexLayout::PositionNormalUv;
    receiver.depth = true;
    receiver.cull_back_faces = true;
    receiver.push_constant_bytes = rhi::push_constant_max_bytes;
    receiver.texture = true;
    receiver.shadow = true;
    auto receiver_pipeline = rhi::GraphicsPipeline::create(device, receiver, error);
    if (!receiver_pipeline) {
        return false;
    }
    impl_->shadow_map = std::move(map);
    impl_->caster_pipeline = std::move(caster_pipeline);
    impl_->receiver_pipeline = std::move(receiver_pipeline);
    impl_->receiver_recipe = recipe(shaders.receiver_vertex, shaders.receiver_fragment, receiver);
    ++impl_->recipes_version;
    impl_->shadow_settings = shadows;
    return true;
}

void Renderer::disable_shadows()
{
    impl_->shadow_settings.reset();
    impl_->casters_drawn = 0;
}

std::optional<Shadows> Renderer::shadows() const
{
    return impl_->shadow_settings;
}

std::size_t Renderer::last_shadow_caster_count() const
{
    return impl_->casters_drawn;
}

bool Renderer::enable_materials(const MaterialShaders& shaders, rhi::RhiError& error)
{
    error = {};
    if (impl_->skinning) {
        error = {rhi::RhiFailure::InvalidArgument, "materials on a skinned renderer are a later slice"};
        return false;
    }
    const rhi::Device& device = *impl_->device;
    std::unique_ptr<rhi::GraphicsPipeline> made[2];
    Recipe recipes[2];
    const std::span<const std::uint32_t> code[2][2] = {{shaders.vertex, shaders.fragment},
                                                       {shaders.shadowed_vertex, shaders.shadowed_fragment}};
    for (int index = 0; index < 2; ++index) {
        const auto vertex = rhi::Shader::load(device, code[index][0], rhi::ShaderStage::Vertex, error);
        const auto fragment = vertex ? rhi::Shader::load(device, code[index][1], rhi::ShaderStage::Fragment, error) : nullptr;
        if (!vertex || !fragment) {
            return false;
        }
        rhi::GraphicsPipelineDesc desc;
        desc.vertex = vertex.get();
        desc.fragment = fragment.get();
        desc.color_format = impl_->pipeline->color_format();
        desc.vertex_layout = rhi::VertexLayout::PositionNormalUv;
        desc.depth = true;
        desc.cull_back_faces = true;
        desc.push_constant_bytes = sizeof(MaterialConstants);
        desc.texture = true;
        desc.shadow = index == 1;
        desc.material = true;
        made[index] = rhi::GraphicsPipeline::create(device, desc, error);
        if (!made[index]) {
            return false;
        }
        recipes[index] = recipe(code[index][0], code[index][1], desc);
    }
    impl_->material_recipes[0] = std::move(recipes[0]);
    impl_->material_recipes[1] = std::move(recipes[1]);
    ++impl_->recipes_version;
    impl_->material_pipeline = std::move(made[0]);
    impl_->material_shadowed_pipeline = std::move(made[1]);
    impl_->materials_on = true;
    return true;
}

void Renderer::disable_materials()
{
    impl_->materials_on = false;
    impl_->materials_drawn = 0;
}

bool Renderer::enable_transparency(const TransparencyShaders& shaders, rhi::RhiError& error)
{
    error = {};
    if (impl_->skinning) {
        error = {rhi::RhiFailure::InvalidArgument, "transparency on a skinned renderer is a later slice"};
        return false;
    }
    const rhi::Device& device = *impl_->device;
    const auto vertex = rhi::Shader::load(device, shaders.vertex, rhi::ShaderStage::Vertex, error);
    const auto fragment = vertex ? rhi::Shader::load(device, shaders.fragment, rhi::ShaderStage::Fragment, error) : nullptr;
    if (!vertex || !fragment) {
        return false;
    }
    rhi::GraphicsPipelineDesc desc;
    desc.vertex = vertex.get();
    desc.fragment = fragment.get();
    desc.color_format = impl_->pipeline->color_format();
    desc.vertex_layout = rhi::VertexLayout::PositionNormalUv;
    desc.depth = true;
    desc.depth_write = false;
    desc.cull_back_faces = true;
    desc.push_constant_bytes = sizeof(TransparentConstants);
    desc.texture = true;
    desc.blend = rhi::BlendMode::Alpha;
    auto made = rhi::GraphicsPipeline::create(device, desc, error);
    if (!made) {
        return false;
    }
    impl_->transparent_pipeline = std::move(made);
    impl_->transparent_recipe = recipe(shaders.vertex, shaders.fragment, desc);
    ++impl_->recipes_version;
    impl_->transparency_on = true;
    return true;
}

void Renderer::disable_transparency()
{
    impl_->transparency_on = false;
    impl_->transparent_drawn = 0;
}

bool Renderer::transparency_enabled() const
{
    return impl_->transparency_on;
}

std::size_t Renderer::last_transparent_draw_count() const
{
    return impl_->transparent_drawn;
}

bool Renderer::enable_post(const PostShaders& shaders, const PostProcess& post, rhi::RhiError& error)
{
    error = {};
    if (impl_->skinning) {
        error = {rhi::RhiFailure::InvalidArgument, "post-processing on a skinned renderer is a later slice"};
        return false;
    }
    if (!std::isfinite(post.exposure) || post.exposure <= 0.F || post.exposure > post_max_exposure) {
        error = {rhi::RhiFailure::InvalidArgument, "exposure must be finite, above 0 and at most 64"};
        return false;
    }
    const rhi::Device& device = *impl_->device;
    rhi::GraphicsPipelineDesc desc;
    desc.vertex_layout = rhi::VertexLayout::PositionNormalUv;
    desc.push_constant_bytes = sizeof(math::Vec4);
    desc.texture = true;
    auto pipeline = build(device, recipe(shaders.vertex, shaders.fragment, desc), impl_->pipeline->color_format(), error);
    if (!pipeline) {
        return false;
    }
    if (!impl_->fullscreen.vertices) {
        // One triangle past the screen's corners; the vertex shader passes positions through.
        const Vertex corners[3] = {{{-1.F, -1.F, 0.F}, {0.F, 0.F, 1.F}, {0.F, 0.F}},
                                   {{3.F, -1.F, 0.F}, {0.F, 0.F, 1.F}, {2.F, 0.F}},
                                   {{-1.F, 3.F, 0.F}, {0.F, 0.F, 1.F}, {0.F, 2.F}}};
        const std::uint32_t indices[3] = {0, 1, 2};
        if (!upload(device, std::span<const Vertex>(corners), indices, impl_->fullscreen, error)) {
            impl_->fullscreen = {};
            return false;
        }
    }
    if (!impl_->ensure_hdr(error)) {
        // Some float pipelines may be gone: off, and rebuilt in full by the next enable.
        impl_->post_settings.reset();
        impl_->hdr_version = 0;
        return false;
    }
    impl_->post_settings = post;
    impl_->post_pipeline = std::move(pipeline);
    return true;
}

void Renderer::disable_post()
{
    impl_->post_settings.reset();
}

std::optional<PostProcess> Renderer::post() const
{
    return impl_->post_settings;
}

bool Renderer::materials_enabled() const
{
    return impl_->materials_on;
}

std::size_t Renderer::last_material_draw_count() const
{
    return impl_->materials_drawn;
}

}
