#include <lykoware/render.hpp>

#include <lykoware/rhi/buffer.hpp>
#include <lykoware/rhi/texture.hpp>

#include "world_identity.hpp"

#include <array>
#include <cmath>
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

struct Vertex {
    math::Vec3 position;
    math::Vec3 normal;
    math::Vec2 uv;
};
static_assert(sizeof(Vertex) == rhi::position_normal_uv_stride);

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
};

// One draw: a part of a mesh with the texture it uses.
struct Drawn {
    const GpuMesh* mesh = nullptr;
    const rhi::Texture* texture = nullptr;
    std::uint32_t first_index = 0;
    std::uint32_t index_count = 0;
};

bool upload(const rhi::Device& device, std::span<const Vertex> vertices, std::span<const std::uint32_t> indices,
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

    const rhi::Texture* texture_for(const MeshRenderer& renderer) const
    {
        if (renderer.texture == TextureId{}) {
            return white.get();
        }
        if (renderer.texture.owner != owner || renderer.texture.value > textures.size()) {
            return nullptr;
        }
        return textures[renderer.texture.value - 1].get();
    }

    const GpuMesh* mesh_for(const MeshRenderer& renderer) const
    {
        if (renderer.mesh == MeshId{}) {
            return &cube;
        }
        if (renderer.mesh.owner != owner || renderer.mesh.value > meshes.size()) {
            return nullptr;
        }
        return &meshes[renderer.mesh.value - 1];
    }

    // Builds one draw per entity with Transform and MeshRenderer, relative to the camera.
    void prepare(const ecs::World& entities, const Camera& camera, float aspect)
    {
        constants.clear();
        drawn.clear();
        draws.clear();
        entities_drawn = 0;
        const math::Mat4 view =
            math::look_at({0.F, 0.F, 0.F}, math::relative_to(camera.target, camera.eye), camera.up);
        const math::Mat4 projection = math::perspective(camera.vertical_fov, aspect, camera.near);
        const math::Mat4 view_projection = projection * view;
        entities.each<Transform, MeshRenderer>([&](ecs::Entity, const Transform& transform, const MeshRenderer& mesh) {
            const GpuMesh* gpu = mesh_for(mesh);
            const rhi::Texture* texture = texture_for(mesh);
            if (gpu == nullptr || texture == nullptr) {
                return; // a mesh or texture id this renderer did not make
            }
            const math::Mat4 model =
                math::trs(math::relative_to(transform.position, camera.eye), transform.rotation, mesh.scale);
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
                c.normal[column] = {rotation.m[column][0] * s, rotation.m[column][1] * s, rotation.m[column][2] * s, 0.F};
            }
            for (const MeshPart& part : gpu->parts) {
                c.colour = {mesh.colour.x * part.colour.x, mesh.colour.y * part.colour.y, mesh.colour.z * part.colour.z, 1.F};
                constants.push_back(c);
                const rhi::Texture* part_texture =
                    part.texture == TextureId{} ? texture : textures[part.texture.value - 1].get();
                drawn.push_back({gpu, part_texture, part.first_index, part.index_count});
            }
            ++entities_drawn;
        });
        // Push-constant spans point into `constants`, so build them after it stops growing.
        for (std::size_t i = 0; i < constants.size(); ++i) {
            draws.push_back({drawn[i].mesh->vertices.get(), drawn[i].mesh->indices.get(), drawn[i].index_count,
                             std::as_bytes(std::span(&constants[i], 1)), drawn[i].texture, drawn[i].first_index});
        }
    }
};

Renderer::Renderer(std::unique_ptr<Impl> impl)
    : impl_{std::move(impl)}
{
}

Renderer::~Renderer() = default;

std::unique_ptr<Renderer> Renderer::create(const rhi::Device& device, rhi::ColorFormat color_format,
                                           std::span<const std::uint32_t> lit_vertex,
                                           std::span<const std::uint32_t> lit_fragment, rhi::RhiError& error)
{
    auto impl = std::make_unique<Impl>();
    impl->device = &device;
    impl->owner = detail::next_world_identity();
    {
        const auto vertex = rhi::Shader::load(device, lit_vertex, rhi::ShaderStage::Vertex, error);
        const auto fragment = vertex ? rhi::Shader::load(device, lit_fragment, rhi::ShaderStage::Fragment, error) : nullptr;
        if (!vertex || !fragment) {
            return nullptr;
        }
        rhi::GraphicsPipelineDesc desc;
        desc.vertex = vertex.get();
        desc.fragment = fragment.get();
        desc.color_format = color_format;
        desc.vertex_layout = rhi::VertexLayout::PositionNormalUv;
        desc.depth = true;
        desc.cull_back_faces = true;
        desc.push_constant_bytes = rhi::push_constant_max_bytes;
        desc.texture = true;
        impl->pipeline = rhi::GraphicsPipeline::create(device, desc, error);
        if (!impl->pipeline) {
            return nullptr;
        }
    }
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    build_cube(vertices, indices);
    if (!upload(device, vertices, indices, impl->cube, error)) {
        return nullptr;
    }
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
    return std::unique_ptr<Renderer>(new Renderer(std::move(impl)));
}

bool Renderer::draw(const ecs::World& entities, const Camera& camera, rhi::RenderTarget& target,
                    const rhi::ClearColor& clear, rhi::RhiError& error)
{
    impl_->prepare(entities, camera, static_cast<float>(target.width()) / static_cast<float>(target.height()));
    return target.draw_meshes(clear, *impl_->pipeline, impl_->draws, error);
}

bool Renderer::draw(const ecs::World& entities, const Camera& camera, rhi::Swapchain& swapchain,
                    const rhi::ClearColor& clear, rhi::RhiError& error)
{
    const float aspect = swapchain.width() > 0 && swapchain.height() > 0
        ? static_cast<float>(swapchain.width()) / static_cast<float>(swapchain.height())
        : 1.F;
    impl_->prepare(entities, camera, aspect);
    return swapchain.render_meshes(clear, *impl_->pipeline, impl_->draws, error);
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
        if (part.texture.value > impl_->textures.size()) {
            error = {rhi::RhiFailure::InvalidArgument, "a mesh part names a texture this renderer did not make"};
            return std::nullopt;
        }
    }
    std::vector<Vertex> vertices(mesh.positions.size());
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        vertices[i] = {mesh.positions[i], mesh.normals[i], mesh.uvs.empty() ? math::Vec2{} : mesh.uvs[i]};
    }
    GpuMesh gpu;
    if (!upload(*impl_->device, vertices, mesh.indices, gpu, error)) {
        return std::nullopt;
    }
    gpu.parts.assign(parts.begin(), parts.end());
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

}
