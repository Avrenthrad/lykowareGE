// assets.gltf: Lykoware's glTF 2.0 reader (ADR 0035). Every model is built here, in memory or in
// the build directory, so the repository holds no model files.

#include <lykoware/gltf.hpp>
#include <lykoware/mesh.hpp>

#include "support/png_writer.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace gltf = lykoware::gltf;
using lykoware::MeshData;
using lykoware::math::Vec3;

int g_failures = 0;

void expect(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

bool near(Vec3 a, Vec3 b, float tolerance = 1e-6F)
{
    return std::abs(a.x - b.x) <= tolerance && std::abs(a.y - b.y) <= tolerance && std::abs(a.z - b.z) <= tolerance;
}

// Every triangle's winding agrees with its vertices' normals: counter-clockwise from the front.
bool faces_outwards(const MeshData& mesh)
{
    for (std::size_t t = 0; t < mesh.indices.size(); t += 3) {
        const Vec3 p0 = mesh.positions[mesh.indices[t]];
        const Vec3 face = lykoware::math::cross(mesh.positions[mesh.indices[t + 1]] - p0, mesh.positions[mesh.indices[t + 2]] - p0);
        if (lykoware::math::dot(face, mesh.normals[mesh.indices[t]]) <= 0.F) {
            return false;
        }
    }
    return true;
}

std::string base64(const std::vector<std::byte>& data)
{
    constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (std::size_t i = 0; i < data.size(); i += 3) {
        std::uint32_t bits = static_cast<std::uint32_t>(data[i]) << 16U;
        if (i + 1 < data.size()) {
            bits |= static_cast<std::uint32_t>(data[i + 1]) << 8U;
        }
        if (i + 2 < data.size()) {
            bits |= static_cast<std::uint32_t>(data[i + 2]);
        }
        out += alphabet[(bits >> 18U) & 63U];
        out += alphabet[(bits >> 12U) & 63U];
        out += i + 1 < data.size() ? alphabet[(bits >> 6U) & 63U] : '=';
        out += i + 2 < data.size() ? alphabet[bits & 63U] : '=';
    }
    return out;
}

std::vector<std::byte> bytes_of(std::string_view text)
{
    std::vector<std::byte> out(text.size());
    std::memcpy(out.data(), text.data(), text.size());
    return out;
}

// Builds a glTF model: binary data, buffer views and accessors are added by calls; meshes,
// nodes and scenes are written as JSON by the test.
struct Model {
    std::vector<std::byte> bin;
    std::vector<std::string> views;
    std::vector<std::string> accessors;
    std::string meshes = "[]";
    std::string nodes = "[{\"mesh\":0}]";
    std::string scenes = "[{\"nodes\":[0]}]";
    std::string extra; // more top-level members, each starting with a comma

    template <typename T>
    std::size_t view(const std::vector<T>& values, std::size_t stride = 0)
    {
        while (bin.size() % 4 != 0) {
            bin.push_back(std::byte{0});
        }
        const std::size_t offset = bin.size();
        bin.resize(offset + values.size() * sizeof(T));
        std::memcpy(bin.data() + offset, values.data(), values.size() * sizeof(T));
        views.push_back("{\"buffer\":0,\"byteOffset\":" + std::to_string(offset) + ",\"byteLength\":"
                        + std::to_string(values.size() * sizeof(T))
                        + (stride != 0 ? ",\"byteStride\":" + std::to_string(stride) : std::string{}) + "}");
        return views.size() - 1;
    }

    std::size_t accessor(std::size_t view_index, std::uint32_t component, std::size_t count, const char* type,
                         std::size_t offset = 0)
    {
        accessors.push_back("{\"bufferView\":" + std::to_string(view_index) + ",\"byteOffset\":" + std::to_string(offset)
                            + ",\"componentType\":" + std::to_string(component) + ",\"count\":" + std::to_string(count)
                            + ",\"type\":\"" + type + "\"}");
        return accessors.size() - 1;
    }

    std::size_t vec3s(const std::vector<Vec3>& values)
    {
        return accessor(view(values), 5126, values.size(), "VEC3");
    }

    std::size_t vec2s(const std::vector<lykoware::math::Vec2>& values)
    {
        return accessor(view(values), 5126, values.size(), "VEC2");
    }

    template <typename T>
    std::size_t indices(const std::vector<T>& values)
    {
        constexpr std::uint32_t component = sizeof(T) == 1 ? 5121 : sizeof(T) == 2 ? 5123 : 5125;
        return accessor(view(values), component, values.size(), "SCALAR");
    }

    static std::string join(const std::vector<std::string>& items)
    {
        std::string out = "[";
        for (std::size_t i = 0; i < items.size(); ++i) {
            out += (i != 0 ? "," : "") + items[i];
        }
        return out + "]";
    }

    // The JSON, with the buffer given by `buffer` (a full buffer object).
    std::string json(const std::string& buffer) const
    {
        return "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[" + buffer + "],\"bufferViews\":" + join(views)
            + ",\"accessors\":" + join(accessors) + ",\"meshes\":" + meshes + ",\"nodes\":" + nodes
            + ",\"scenes\":" + scenes + extra + "}";
    }

    std::vector<std::byte> gltf() const
    {
        return bytes_of(json("{\"byteLength\":" + std::to_string(bin.size()) + ",\"uri\":\"data:application/octet-stream;base64,"
                             + base64(bin) + "\"}"));
    }

    std::vector<std::byte> glb() const
    {
        std::string text = json("{\"byteLength\":" + std::to_string(bin.size()) + "}");
        while (text.size() % 4 != 0) {
            text += ' ';
        }
        std::vector<std::byte> padded = bin;
        while (padded.size() % 4 != 0) {
            padded.push_back(std::byte{0});
        }
        std::vector<std::byte> out;
        auto u32 = [&](std::uint32_t v) {
            for (int i = 0; i < 4; ++i) {
                out.push_back(static_cast<std::byte>((v >> (8 * i)) & 0xFFU));
            }
        };
        out.insert(out.end(), {std::byte{'g'}, std::byte{'l'}, std::byte{'T'}, std::byte{'F'}});
        u32(2);
        u32(static_cast<std::uint32_t>(12 + 8 + text.size() + 8 + padded.size()));
        u32(static_cast<std::uint32_t>(text.size()));
        u32(0x4E4F534AU);
        const std::vector<std::byte> json_bytes = bytes_of(text);
        out.insert(out.end(), json_bytes.begin(), json_bytes.end());
        u32(static_cast<std::uint32_t>(padded.size()));
        u32(0x004E4942U);
        out.insert(out.end(), padded.begin(), padded.end());
        return out;
    }
};

std::optional<MeshData> read(const std::vector<std::byte>& file, std::string& error, const gltf::BufferReader& external = {})
{
    return gltf::read(file, external, error);
}

// A unit quad in the XY plane facing +Z: four vertices with normals, two triangles.
Model quad()
{
    Model m;
    const std::size_t p = m.vec3s({{0.F, 0.F, 0.F}, {1.F, 0.F, 0.F}, {1.F, 1.F, 0.F}, {0.F, 1.F, 0.F}});
    const std::size_t n = m.vec3s(std::vector<Vec3>(4, Vec3{0.F, 0.F, 1.F}));
    const std::size_t i = m.indices(std::vector<std::uint16_t>{0, 1, 2, 0, 2, 3});
    m.meshes = "[{\"primitives\":[{\"attributes\":{\"POSITION\":" + std::to_string(p) + ",\"NORMAL\":" + std::to_string(n)
        + "},\"indices\":" + std::to_string(i) + "}]}]";
    return m;
}

void test_basic()
{
    std::string error;
    // One triangle, positions only: flat normals are made, facing the counter-clockwise side.
    Model triangle;
    const std::size_t p = triangle.vec3s({{0.F, 0.F, 0.F}, {1.F, 0.F, 0.F}, {0.F, 1.F, 0.F}});
    triangle.meshes = "[{\"primitives\":[{\"attributes\":{\"POSITION\":" + std::to_string(p) + "}}]}]";
    auto mesh = read(triangle.gltf(), error);
    expect(mesh && mesh->positions.size() == 3 && mesh->indices == std::vector<std::uint32_t>{0, 1, 2},
           "a triangle without indices or normals loads");
    expect(mesh && mesh->normals[0] == Vec3{0.F, 0.F, 1.F} && mesh->normals[2] == Vec3{0.F, 0.F, 1.F},
           "missing normals become the face normal");

    // An indexed quad with normals, as .gltf with a data URI and as .glb: the same mesh.
    const Model square = quad();
    const auto from_gltf = read(square.gltf(), error);
    const auto from_glb = read(square.glb(), error);
    expect(from_gltf && from_gltf->positions.size() == 4 && from_gltf->indices == std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3},
           "an indexed quad with normals loads from .gltf");
    expect(from_glb && from_gltf && from_glb->positions == from_gltf->positions && from_glb->normals == from_gltf->normals
               && from_glb->indices == from_gltf->indices,
           "the same model as .glb loads identically");
    expect(from_gltf && faces_outwards(*from_gltf), "the quad's triangles face along their normals");
    const auto box = lykoware::bounds(*from_gltf);
    expect(box.min == Vec3{0.F, 0.F, 0.F} && box.max == Vec3{1.F, 1.F, 0.F}, "bounds span the quad");

    // 8- and 32-bit indices.
    for (const int bits : {8, 32}) {
        Model m;
        const std::size_t pos = m.vec3s({{0.F, 0.F, 0.F}, {1.F, 0.F, 0.F}, {0.F, 1.F, 0.F}});
        const std::size_t idx = bits == 8 ? m.indices(std::vector<std::uint8_t>{0, 1, 2}) : m.indices(std::vector<std::uint32_t>{0, 1, 2});
        m.meshes = "[{\"primitives\":[{\"attributes\":{\"POSITION\":" + std::to_string(pos) + "},\"indices\":" + std::to_string(idx) + "}]}]";
        mesh = read(m.gltf(), error);
        expect(mesh && mesh->indices.size() == 3, bits == 8 ? "8-bit indices load" : "32-bit indices load");
    }

    // Positions and normals interleaved in one buffer view with a 24-byte stride.
    Model interleaved;
    const std::size_t v = interleaved.view(std::vector<Vec3>{{0.F, 0.F, 0.F}, {0.F, 0.F, 1.F}, {1.F, 0.F, 0.F}, {0.F, 0.F, 1.F},
                                                             {0.F, 1.F, 0.F}, {0.F, 0.F, 1.F}},
                                           24);
    const std::size_t ip = interleaved.accessor(v, 5126, 3, "VEC3", 0);
    const std::size_t in = interleaved.accessor(v, 5126, 3, "VEC3", 12);
    interleaved.meshes = "[{\"primitives\":[{\"attributes\":{\"POSITION\":" + std::to_string(ip) + ",\"NORMAL\":" + std::to_string(in) + "}}]}]";
    mesh = read(interleaved.gltf(), error);
    expect(mesh && mesh->positions[1] == Vec3{1.F, 0.F, 0.F} && mesh->normals[1] == Vec3{0.F, 0.F, 1.F},
           "interleaved data with a byte stride loads");
}

void test_nodes()
{
    std::string error;
    Model m = quad();

    m.nodes = "[{\"mesh\":0,\"translation\":[1,2,3]}]";
    auto mesh = read(m.gltf(), error);
    expect(mesh && mesh->positions[2] == Vec3{2.F, 3.F, 3.F}, "a node's translation moves its mesh");

    // A parent scaled by 2 with a child moved 1 along X: the child's mesh is scaled, then moved 2.
    m.nodes = "[{\"scale\":[2,2,2],\"children\":[1]},{\"mesh\":0,\"translation\":[1,0,0]}]";
    mesh = read(m.gltf(), error);
    expect(mesh && mesh->positions[2] == Vec3{4.F, 2.F, 0.F}, "transforms combine down the hierarchy");
    m.nodes = "[{\"matrix\":[2,0,0,0, 0,2,0,0, 0,0,2,0, 0,0,0,1],\"children\":[1]},{\"mesh\":0,\"matrix\":[1,0,0,0, 0,1,0,0, 0,0,1,0, 1,0,0,1]}]";
    const auto from_matrices = read(m.gltf(), error);
    expect(from_matrices && mesh && from_matrices->positions == mesh->positions, "matrices give the same result as TRS");

    // A quarter turn about +Y turns the +Z normal to +X.
    m.nodes = "[{\"mesh\":0,\"rotation\":[0,0.70710678,0,0.70710678]}]";
    mesh = read(m.gltf(), error);
    expect(mesh && near(mesh->normals[0], {1.F, 0.F, 0.F}) && near(mesh->positions[1], {0.F, 0.F, -1.F}),
           "a node's rotation turns positions and normals");

    // Uneven scale: a slanted normal goes through the inverse transpose, not the scale itself.
    Model slanted;
    const float s = std::sqrt(0.5F);
    const std::size_t p = slanted.vec3s({{0.F, 0.F, 0.F}, {0.F, 0.F, 1.F}, {1.F, -1.F, 0.F}});
    const std::size_t n = slanted.vec3s(std::vector<Vec3>(3, Vec3{s, s, 0.F}));
    slanted.meshes = "[{\"primitives\":[{\"attributes\":{\"POSITION\":" + std::to_string(p) + ",\"NORMAL\":" + std::to_string(n) + "}}]}]";
    slanted.nodes = "[{\"mesh\":0,\"scale\":[1,2,1]}]";
    mesh = read(slanted.gltf(), error);
    const float k = 1.F / std::sqrt(5.F);
    expect(mesh && near(mesh->normals[0], {2.F * k, k, 0.F}), "uneven scale bends normals by the inverse transpose");

    // A mirror (negative scale) reverses the winding so triangles still face outwards.
    m.nodes = "[{\"mesh\":0,\"scale\":[-1,1,1]}]";
    mesh = read(m.gltf(), error);
    expect(mesh && mesh->indices == std::vector<std::uint32_t>{0, 2, 1, 0, 3, 2} && faces_outwards(*mesh),
           "a mirrored node keeps its triangles facing outwards");

    // Two scenes: "scene" picks which one loads.
    m.nodes = "[{\"mesh\":0},{\"mesh\":0,\"translation\":[10,0,0]}]";
    m.scenes = "[{\"nodes\":[0]},{\"nodes\":[1]}]";
    m.extra = ",\"scene\":1";
    mesh = read(m.gltf(), error);
    expect(mesh && mesh->positions[0] == Vec3{10.F, 0.F, 0.F}, "the default scene is the one named by \"scene\"");
    m.extra.clear();
    m.scenes = "[{\"nodes\":[0,1]}]";
    mesh = read(m.gltf(), error);
    expect(mesh && mesh->positions.size() == 8 && mesh->indices.size() == 12 && mesh->indices[6] == 4,
           "every mesh in the scene is merged, with indices offset");
}

void test_modes()
{
    std::string error;
    Model m;
    // Given normals facing +Z, so the unrolled triangles' winding must agree with them.
    const std::size_t p = m.vec3s({{0.F, 0.F, 0.F}, {1.F, 0.F, 0.F}, {0.F, 1.F, 0.F}, {1.F, 1.F, 0.F}});
    const std::size_t n = m.vec3s(std::vector<Vec3>(4, Vec3{0.F, 0.F, 1.F}));
    const std::string attributes = "{\"POSITION\":" + std::to_string(p) + ",\"NORMAL\":" + std::to_string(n) + "}";
    m.meshes = "[{\"primitives\":[{\"attributes\":" + attributes + ",\"mode\":5}]}]";
    auto mesh = read(m.gltf(), error);
    expect(mesh && mesh->indices == std::vector<std::uint32_t>{0, 1, 2, 1, 3, 2} && faces_outwards(*mesh),
           "a triangle strip unrolls into front-facing triangles");
    // A fan around the first vertex of a convex quad, counter-clockwise from +Z.
    Model fan;
    const std::size_t fp = fan.vec3s({{0.F, 0.F, 0.F}, {1.F, 0.F, 0.F}, {1.F, 1.F, 0.F}, {0.F, 1.F, 0.F}});
    const std::size_t fn = fan.vec3s(std::vector<Vec3>(4, Vec3{0.F, 0.F, 1.F}));
    fan.meshes = "[{\"primitives\":[{\"attributes\":{\"POSITION\":" + std::to_string(fp) + ",\"NORMAL\":" + std::to_string(fn)
        + "},\"mode\":6}]}]";
    mesh = read(fan.gltf(), error);
    expect(mesh && mesh->indices == std::vector<std::uint32_t>{1, 2, 0, 2, 3, 0} && faces_outwards(*mesh),
           "a triangle fan unrolls into front-facing triangles");
    m.meshes = "[{\"primitives\":[{\"attributes\":" + attributes + ",\"mode\":1},{\"attributes\":" + attributes + ",\"mode\":5}]}]";
    mesh = read(m.gltf(), error);
    expect(mesh && mesh->indices.size() == 6, "a line primitive is skipped beside triangles");
    m.meshes = "[{\"primitives\":[{\"attributes\":" + attributes + ",\"mode\":1}]}]";
    expect(!read(m.gltf(), error) && error.find("no triangles") != std::string::npos, "a model of lines only has no triangles");
}

void test_refusals()
{
    std::string error;
    auto refused = [&](const std::vector<std::byte>& file, std::string_view what) {
        error.clear();
        expect(!read(file, error) && !error.empty(), what);
    };
    const Model square = quad();
    refused(bytes_of("not json"), "text that is not JSON is refused");
    refused(bytes_of("{\"asset\":{\"version\":\"1.0\"},\"scenes\":[{}]}"), "glTF 1.0 is refused");
    Model m = square;
    m.extra = ",\"extensionsRequired\":[\"KHR_draco_mesh_compression\"]";
    refused(m.gltf(), "a required extension is refused");
    expect(error.find("KHR_draco_mesh_compression") != std::string::npos, "the refusal names the extension");
    m = square;
    m.scenes = "[]";
    refused(m.gltf(), "a model without a scene is refused");
    m = square;
    m.nodes = "[{\"mesh\":0,\"children\":[0]}]";
    refused(m.gltf(), "a node that is its own child is refused");
    m.nodes = "[{\"children\":[2]},{\"children\":[2]},{\"mesh\":0}]";
    m.scenes = "[{\"nodes\":[0,1]}]";
    refused(m.gltf(), "a node with two parents is refused");
    m = square;
    m.nodes = "[{\"mesh\":0,\"matrix\":[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],\"scale\":[2,2,2]}]";
    refused(m.gltf(), "a node with a matrix and a scale is refused");
    m = square;
    m.nodes = "[{\"mesh\":5}]";
    refused(m.gltf(), "a node naming a missing mesh is refused");

    // Data out of bounds.
    m = square;
    m.accessors[0] = "{\"bufferView\":0,\"byteOffset\":4,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"}";
    refused(m.gltf(), "an accessor past the end of its view is refused");
    m = square;
    m.views[0] = "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":100000}";
    refused(m.gltf(), "a view past the end of its buffer is refused");
    m = square;
    m.accessors[2] = "{\"bufferView\":2,\"componentType\":5123,\"count\":6,\"type\":\"SCALAR\",\"sparse\":{}}";
    refused(m.gltf(), "a sparse accessor is refused");
    m = square;
    m.accessors[0] = "{\"bufferView\":0,\"componentType\":5123,\"count\":4,\"type\":\"VEC3\"}";
    refused(m.gltf(), "16-bit positions are refused");

    Model bad_index;
    const std::size_t p = bad_index.vec3s({{0.F, 0.F, 0.F}, {1.F, 0.F, 0.F}, {0.F, 1.F, 0.F}});
    const std::size_t i = bad_index.indices(std::vector<std::uint16_t>{0, 1, 3});
    bad_index.meshes = "[{\"primitives\":[{\"attributes\":{\"POSITION\":" + std::to_string(p) + "},\"indices\":" + std::to_string(i) + "}]}]";
    refused(bad_index.gltf(), "an index past the last vertex is refused");

    Model nan_position;
    const std::size_t q = nan_position.vec3s({{0.F, 0.F, 0.F}, {std::nanf(""), 0.F, 0.F}, {0.F, 1.F, 0.F}});
    nan_position.meshes = "[{\"primitives\":[{\"attributes\":{\"POSITION\":" + std::to_string(q) + "}}]}]";
    refused(nan_position.gltf(), "a NaN position is refused");

    m = square;
    m.accessors[1] = "{\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"}";
    refused(m.gltf(), "normals that do not match the positions are refused");

    // Broken containers and buffers.
    std::vector<std::byte> glb = square.glb();
    glb[4] = std::byte{1};
    refused(glb, ".glb version 1 is refused");
    glb = square.glb();
    glb.resize(glb.size() - 8);
    refused(glb, "a truncated .glb is refused");
    m = square;
    std::string text = m.json("{\"byteLength\":8,\"uri\":\"data:application/octet-stream;base64,AAAA*AAA\"}");
    refused(bytes_of(text), "base64 with a bad character is refused");
    text = m.json("{\"byteLength\":4,\"uri\":\"mesh.bin\"}");
    refused(bytes_of(text), "an external buffer without a reader is refused");
}

void test_external_buffers(const std::filesystem::path& directory)
{
    const Model square = quad();
    const std::string json = square.json("{\"byteLength\":" + std::to_string(square.bin.size()) + ",\"uri\":\"my%20mesh.bin\"}");
    std::string asked;
    std::string error;
    const auto mesh = gltf::read(bytes_of(json), [&](std::string_view uri) -> std::optional<std::vector<std::byte>> {
        asked = uri;
        return square.bin;
    }, error);
    expect(mesh && asked == "my mesh.bin", "an external buffer is read through the reader, its URI percent-decoded");

    // From files: the .gltf and its .bin side by side.
    std::filesystem::create_directories(directory / "models");
    auto write = [](const std::filesystem::path& path, const std::vector<std::byte>& data) {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    };
    write(directory / "models" / "my mesh.bin", square.bin);
    write(directory / "secret.bin", square.bin);
    write(directory / "models" / "quad.gltf", bytes_of(json));
    const auto loaded = gltf::load(directory / "models" / "quad.gltf", error);
    expect(loaded && loaded->positions.size() == 4, "load() reads a .gltf with its .bin beside it");
    write(directory / "models" / "quad.glb", square.glb());
    expect(gltf::load(directory / "models" / "quad.glb", error).has_value(), "load() reads a .glb");

    // A buffer outside the model's folder is not read.
    for (const std::string uri : {"../secret.bin", "/etc/passwd", "file:///etc/passwd", "sub/../../secret.bin"}) {
        write(directory / "models" / "escape.gltf",
              bytes_of(square.json("{\"byteLength\":" + std::to_string(square.bin.size()) + ",\"uri\":\"" + uri + "\"}")));
        expect(!gltf::load(directory / "models" / "escape.gltf", error), "a buffer outside the model's folder is refused: " + uri);
    }
    expect(!gltf::load(directory / "models" / "missing.gltf", error) && !error.empty(), "a missing file is refused");
}

// --- Materials (ADR 0038) -------------------------------------------------------------------

using lykoware::math::Vec2;

// A 2x1 PNG: red then blue.
std::vector<std::byte> red_blue_png()
{
    return lykoware::test::png_rgba(2, 1, {255, 0, 0, 255, 0, 0, 255, 255});
}

// The unit quad with texture coordinates, a material with a factor and a texture, its sampler,
// and the PNG in a buffer view. `material_json` replaces the material when given.
Model textured_quad(const std::string& material_json = "")
{
    Model m = quad();
    const std::size_t uv = m.vec2s({{0.F, 1.F}, {1.F, 1.F}, {1.F, 0.F}, {0.F, 0.F}});
    const std::size_t image_view = m.view(red_blue_png());
    m.meshes = "[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":" + std::to_string(uv)
        + "},\"indices\":2,\"material\":0}]}]";
    m.extra = ",\"materials\":["
        + (material_json.empty() ? std::string{"{\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.5,0.25,1,1],\"baseColorTexture\":{\"index\":0}}}"}
                                 : material_json)
        + "],\"textures\":[{\"source\":0,\"sampler\":0}],"
          "\"samplers\":[{\"wrapS\":33071,\"wrapT\":33071,\"magFilter\":9728,\"minFilter\":9729}],"
          "\"images\":[{\"bufferView\":" + std::to_string(image_view) + ",\"mimeType\":\"image/png\"}]";
    return m;
}

void test_materials()
{
    std::string error;
    const Model m = textured_quad();
    auto model = gltf::read_model(m.gltf(), {}, error);
    expect(model.has_value(), "a textured model loads: " + error);
    if (!model) {
        return;
    }
    expect(model->parts.size() == 1 && model->parts[0].first_index == 0 && model->parts[0].index_count == 6
               && model->parts[0].material == std::optional<std::size_t>{0},
           "a primitive becomes one part with its material");
    expect(model->materials.size() == 1 && model->materials[0].base_colour.x == 0.5F && model->materials[0].base_colour.y == 0.25F
               && model->materials[0].base_colour.z == 1.F && model->materials[0].base_colour_texture == std::optional<std::size_t>{0},
           "the material's base colour factor and texture are read");
    expect(model->textures.size() == 1 && model->textures[0].image == 0 && model->textures[0].sampling.wrap == gltf::Wrap::ClampToEdge
               && model->textures[0].sampling.nearest && !model->textures[0].sampling.mipmaps,
           "the sampler's wrap, magnification and mipmap settings are read");
    expect(model->images.size() == 1 && model->images[0].width == 2 && model->images[0].height == 1
               && model->images[0].pixels == std::vector<std::uint8_t>{255, 0, 0, 255, 0, 0, 255, 255},
           "the image in a buffer view is decoded");
    expect(model->mesh.uvs.size() == 4 && model->mesh.uvs[2] == Vec2{1.F, 0.F}, "texture coordinates are read");

    // The same file through .glb, and through read() for the shape alone.
    const auto from_glb = gltf::read_model(m.glb(), {}, error);
    expect(from_glb && from_glb->images.size() == 1 && from_glb->mesh.uvs == model->mesh.uvs, "the same model as .glb reads identically");
    const auto shape = gltf::read(m.gltf(), {}, error);
    expect(shape && shape->positions == model->mesh.positions && shape->indices == model->mesh.indices, "read() gives the same shape");

    // read() does not decode images, so a broken image only stops read_model().
    Model broken = textured_quad();
    broken.bin[broken.bin.size() - 30] ^= std::byte{0xFF};
    expect(gltf::read(broken.gltf(), {}, error).has_value(), "read() ignores materials and their images");
    expect(!gltf::read_model(broken.gltf(), {}, error) && error.find("image 0") != std::string::npos,
           "read_model() refuses an image that does not decode, naming it");

    // Two primitives: one textured with a material, one with no material and no coordinates.
    Model two = textured_quad();
    two.meshes = "[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1},\"indices\":2},"
                 "{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":3},\"indices\":2,\"material\":0},"
                 "{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":3},\"indices\":2,\"material\":0}]}]";
    model = gltf::read_model(two.gltf(), {}, error);
    expect(model && model->parts.size() == 3 && !model->parts[0].material && model->parts[1].first_index == 6
               && model->parts[2].material == std::optional<std::size_t>{0} && model->materials.size() == 1 && model->images.size() == 1,
           "parts follow the primitives; a material used twice is listed once");
    expect(model && model->mesh.uvs.size() == 12 && model->mesh.uvs[0] == Vec2{} && model->mesh.uvs[6] == Vec2{1.F, 0.F},
           "vertices without texture coordinates read (0, 0)");

    // Normalised 8- and 16-bit coordinates; unnormalised integers are refused.
    for (const int bits : {8, 16}) {
        Model n = quad();
        const std::size_t v = bits == 8 ? n.view(std::vector<std::uint8_t>{0, 255, 255, 255, 255, 0, 0, 0})
                                        : n.view(std::vector<std::uint16_t>{0, 65535, 65535, 65535, 65535, 0, 0, 0});
        n.accessors.push_back("{\"bufferView\":" + std::to_string(v) + ",\"componentType\":" + (bits == 8 ? "5121" : "5123")
                              + ",\"normalized\":true,\"count\":4,\"type\":\"VEC2\"}");
        n.meshes = "[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":3},\"indices\":2}]}]";
        model = gltf::read_model(n.gltf(), {}, error);
        expect(model && model->mesh.uvs[1] == Vec2{1.F, 1.F} && model->mesh.uvs[3] == Vec2{0.F, 0.F},
               bits == 8 ? "normalised 8-bit texture coordinates are read" : "normalised 16-bit texture coordinates are read");
        n.accessors.back().replace(n.accessors.back().find("true"), 4, "false");
        expect(!gltf::read_model(n.gltf(), {}, error), "unnormalised integer texture coordinates are refused");
    }

    // Flat normals keep each corner's texture coordinate.
    Model flat;
    const std::size_t fp = flat.vec3s({{0.F, 0.F, 0.F}, {1.F, 0.F, 0.F}, {0.F, 1.F, 0.F}});
    const std::size_t fuv = flat.vec2s({{0.1F, 0.2F}, {0.3F, 0.4F}, {0.5F, 0.6F}});
    flat.meshes = "[{\"primitives\":[{\"attributes\":{\"POSITION\":" + std::to_string(fp) + ",\"TEXCOORD_0\":" + std::to_string(fuv) + "}}]}]";
    flat.nodes = "[{\"mesh\":0,\"scale\":[-1,1,1]}]";
    model = gltf::read_model(flat.gltf(), {}, error);
    expect(model && model->mesh.uvs == std::vector<Vec2>{{0.1F, 0.2F}, {0.5F, 0.6F}, {0.3F, 0.4F}} && faces_outwards(model->mesh),
           "flat normals keep each corner's texture coordinate, mirrored corners included");

    // Refusals.
    auto refused = [&](const Model& model_in, std::string_view what) {
        error.clear();
        expect(!gltf::read_model(model_in.gltf(), {}, error) && !error.empty(), what);
    };
    refused(textured_quad("{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0,\"texCoord\":1}}}"),
            "a texture on a second coordinate set is refused");
    refused(textured_quad("{\"pbrMetallicRoughness\":{\"baseColorFactor\":[1.5,1,1,1]}}"), "a base colour factor above 1 is refused");
    refused(textured_quad("{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":7}}}"), "a missing texture is refused");
    Model no_source = textured_quad();
    no_source.extra.replace(no_source.extra.find("{\"source\":0,"), 12, "{");
    refused(no_source, "a texture without a source image is refused");
    Model bad_wrap = textured_quad();
    bad_wrap.extra.replace(bad_wrap.extra.find("33071"), 5, "12345");
    refused(bad_wrap, "an unknown wrap mode is refused");
    Model webp = textured_quad();
    webp.extra.replace(webp.extra.find("image/png"), 9, "image/webp");
    refused(webp, "an image in a buffer view with another mime type is refused");
}

void test_material_files(const std::filesystem::path& directory)
{
    std::filesystem::create_directories(directory / "models");
    auto write = [](const std::filesystem::path& path, const std::vector<std::byte>& data) {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    };
    // The image in its own file beside the model, and as a data URI.
    for (const std::string& uri : {std::string{"red%20blue.png"}, "data:image/png;base64," + lykoware::test::base64(red_blue_png())}) {
        Model m = textured_quad();
        m.extra.replace(m.extra.find("{\"bufferView\""), m.extra.find("}]", m.extra.find("\"images\"")) - m.extra.find("{\"bufferView\"") + 1,
                        "{\"uri\":\"" + uri + "\"}");
        write(directory / "models" / "red blue.png", red_blue_png());
        write(directory / "models" / "file-image.gltf", m.gltf());
        std::string error;
        const auto model = gltf::load_model(directory / "models" / "file-image.gltf", error);
        expect(model && model->images.size() == 1 && model->images[0].pixels[8 - 2] == 255,
               (uri.starts_with("data:") ? "an image in a data URI loads" : "an image in a file beside the model loads") + std::string{": "} + error);
    }
    Model escape = textured_quad();
    escape.extra.replace(escape.extra.find("{\"bufferView\""), escape.extra.find("}]", escape.extra.find("\"images\"")) - escape.extra.find("{\"bufferView\"") + 1,
                         "{\"uri\":\"../outside.png\"}");
    write(directory / "outside.png", red_blue_png());
    write(directory / "models" / "escape-image.gltf", escape.gltf());
    std::string error;
    expect(!gltf::load_model(directory / "models" / "escape-image.gltf", error), "an image outside the model's folder is refused");

    // The textured quad as .glb, for sandbox.model (a CTest fixture).
    write(directory / "models" / "textured.glb", textured_quad().glb());
}

void test_valid_mesh()
{
    std::string error;
    MeshData mesh{{{0.F, 0.F, 0.F}, {1.F, 0.F, 0.F}, {0.F, 1.F, 0.F}}, {{0.F, 0.F, 1.F}, {0.F, 0.F, 1.F}, {0.F, 0.F, 1.F}}, {0, 1, 2}, {}};
    expect(lykoware::valid_mesh(mesh, error), "a triangle is a valid mesh");
    mesh.indices = {0, 1, 3};
    expect(!lykoware::valid_mesh(mesh, error), "an index out of range is invalid");
    mesh.indices = {0, 1};
    expect(!lykoware::valid_mesh(mesh, error), "part of a triangle is invalid");
    mesh.indices = {0, 1, 2};
    mesh.normals[1] = {0.F, 0.F, 2.F};
    expect(!lykoware::valid_mesh(mesh, error), "a normal that is not unit length is invalid");
    mesh.normals.pop_back();
    expect(!lykoware::valid_mesh(mesh, error), "a missing normal is invalid");
    expect(!lykoware::valid_mesh(MeshData{}, error), "an empty mesh is invalid");
}

}

int main(int argc, char** argv)
{
    const std::filesystem::path directory = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("gltf-test");
    test_basic();
    test_nodes();
    test_modes();
    test_refusals();
    test_external_buffers(directory);
    test_valid_mesh();
    test_materials();
    test_material_files(directory);
    if (g_failures != 0) {
        std::cerr << g_failures << " glTF assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "glTF reader passed\n";
    return EXIT_SUCCESS;
}
