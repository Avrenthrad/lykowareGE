#include <lykoware/render.hpp>
#include <lykoware/image_decode.hpp>
#include <lykoware/mesh.hpp>
#include <iostream>
#include <vector>
int main(int argc, char** argv)
{
    namespace rhi = lykoware::rhi;
    namespace render = lykoware::render;
    if (argc != 3) return 2;
    rhi::RhiError error;
    auto instance = rhi::Instance::create({}, error);
    auto device = instance ? rhi::Device::create(*instance, {}, error) : nullptr;
    if (!device) { std::cerr << error.message << '\n'; return 2; }
    std::vector<std::uint32_t> vertex, fragment;
    if (!rhi::read_spirv_file(argv[1], vertex, error) || !rhi::read_spirv_file(argv[2], fragment, error)) return 2;
    lykoware::MeshData mesh;
    mesh.positions = {{-0.5F, -0.5F, 0}, {0.5F, -0.5F, 0}, {0, 0.5F, 0}};
    mesh.normals = {{0,0,1}, {0,0,1}, {0,0,1}};
    mesh.indices = {0,1,2};
    mesh.uvs = {{0,1}, {1,1}, {0.5F,0}};
    lykoware::RgbaImage image{1, 1, {255,0,0,255}};
    auto a = render::Renderer::create(*device, rhi::ColorFormat::Rgba8Unorm, vertex, fragment, error);
    if (!a) return 2;
    auto old_mesh = a->add_mesh(mesh, error);
    auto old_texture = a->add_texture(image, {}, error);
    if (!old_mesh || !old_texture) return 2;
    a.reset();
    auto b = render::Renderer::create(*device, rhi::ColorFormat::Rgba8Unorm, vertex, fragment, error);
    if (!b) return 2;
    image.pixels = {0,0,255,255};
    auto new_mesh = b->add_mesh(mesh, error);
    auto new_texture = b->add_texture(image, {}, error);
    if (!new_mesh || !new_texture) return 2;
    render::MeshPart part;
    part.index_count = 3;
    part.texture = *old_texture;
    const bool accepts_old_texture = b->add_mesh(mesh, std::span(&part, 1), error).has_value();
    auto target = rhi::RenderTarget::create(*device, {64, 64, true}, error);
    lykoware::ecs::World entities;
    auto entity = entities.create();
    entities.add<lykoware::Transform>(entity, lykoware::Transform{});
    render::MeshRenderer component;
    component.mesh = *old_mesh;
    component.texture = *old_texture;
    entities.add<render::MeshRenderer>(entity, component);
    if (!target || !b->draw(entities, {}, *target, {0,0,0,1}, error)) { std::cerr << error.message << '\n'; return 2; }
    const bool draws_old_ids = b->last_draw_count() == 1;
    lykoware::Image captured;
    if (!rhi::RenderTargetCapture{*target}.capture(captured)) return 2;
    const auto pixels = captured.pixels();
    const auto offset = (std::size_t{32} * 64 + 32) * 3;
    std::cout << "centre_rgb=" << static_cast<unsigned>(pixels[offset]) << ','
              << static_cast<unsigned>(pixels[offset + 1]) << ',' << static_cast<unsigned>(pixels[offset + 2]) << '\n';
    std::cout << "mesh_ids_equal=" << (*old_mesh == *new_mesh) << " texture_ids_equal=" << (*old_texture == *new_texture)
              << " accepts_old_texture_in_mesh=" << accepts_old_texture << " draws_old_renderer_ids=" << draws_old_ids << '\n';
    return accepts_old_texture && draws_old_ids ? 1 : 0;
}
