#include <lykoware/gltf.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <string>
#include <system_error>
#include <utility>

namespace lykoware::gltf {

namespace {

using Json = nlohmann::json;

// The reason a model is refused, thrown inside the reader and caught in read().
struct Refusal {
    std::string reason;
};

[[noreturn]] void refuse(std::string reason)
{
    throw Refusal{std::move(reason)};
}

std::string name(std::string_view what, std::size_t index)
{
    return std::string{what} + " " + std::to_string(index);
}

std::uint32_t u32_at(std::span<const std::byte> bytes, std::size_t at)
{
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        value |= static_cast<std::uint32_t>(bytes[at + i]) << (8U * i);
    }
    return value;
}

// --- JSON fields ----------------------------------------------------------------------------

const Json* member(const Json& object, const char* key)
{
    const auto found = object.find(key);
    return found == object.end() ? nullptr : &*found;
}

std::optional<std::size_t> optional_index(const Json& object, const char* key, const std::string& where)
{
    const Json* value = member(object, key);
    if (value == nullptr) {
        return std::nullopt;
    }
    if (!value->is_number_unsigned() || value->get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max()) {
        refuse(where + ": " + key + " must be a non-negative integer");
    }
    return static_cast<std::size_t>(value->get<std::uint64_t>());
}

std::size_t required_index(const Json& object, const char* key, const std::string& where)
{
    const auto value = optional_index(object, key, where);
    if (!value) {
        refuse(where + " has no " + key);
    }
    return *value;
}

const Json& array_field(const Json& object, const char* key, const std::string& where)
{
    static const Json empty = Json::array();
    const Json* value = member(object, key);
    if (value == nullptr) {
        return empty;
    }
    if (!value->is_array()) {
        refuse(where + ": " + key + " must be an array");
    }
    return *value;
}

const Json& element(const Json& array, std::size_t index, std::string_view what)
{
    if (index >= array.size()) {
        refuse(name(what, index) + " does not exist");
    }
    const Json& value = array[index];
    if (!value.is_object()) {
        refuse(name(what, index) + " must be an object");
    }
    return value;
}

template <std::size_t N>
std::array<float, N> numbers(const Json& value, const std::string& where)
{
    if (!value.is_array() || value.size() != N) {
        refuse(where + " must be an array of " + std::to_string(N) + " numbers");
    }
    std::array<float, N> out{};
    for (std::size_t i = 0; i < N; ++i) {
        const double number = value[i].is_number() ? value[i].get<double>() : std::nan("");
        if (!std::isfinite(number) || std::abs(number) > std::numeric_limits<float>::max()) {
            refuse(where + " must hold finite numbers");
        }
        out[i] = static_cast<float>(number);
    }
    return out;
}

// --- Buffers --------------------------------------------------------------------------------

std::vector<std::byte> decode_base64(std::string_view text, const std::string& where)
{
    auto value_of = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') {
            return c - 'A';
        }
        if (c >= 'a' && c <= 'z') {
            return c - 'a' + 26;
        }
        if (c >= '0' && c <= '9') {
            return c - '0' + 52;
        }
        return c == '+' ? 62 : c == '/' ? 63 : -1;
    };
    if (text.size() % 4 != 0) {
        refuse(where + ": the base64 data is not a whole number of 4-character groups");
    }
    const std::size_t padding = text.ends_with("==") ? 2 : text.ends_with('=') ? 1 : 0;
    std::vector<std::byte> out;
    out.reserve(text.size() / 4 * 3);
    for (std::size_t group = 0; group < text.size(); group += 4) {
        std::uint32_t bits = 0;
        for (std::size_t i = 0; i < 4; ++i) {
            const std::size_t at = group + i;
            const bool pad = at >= text.size() - padding;
            const int value = pad ? (text[at] == '=' ? 0 : -1) : value_of(text[at]);
            if (value < 0) {
                refuse(where + ": the base64 data has a character outside the alphabet");
            }
            bits = (bits << 6U) | static_cast<std::uint32_t>(value);
        }
        out.push_back(static_cast<std::byte>(bits >> 16U));
        out.push_back(static_cast<std::byte>((bits >> 8U) & 0xFFU));
        out.push_back(static_cast<std::byte>(bits & 0xFFU));
    }
    out.resize(out.size() - padding);
    return out;
}

std::string percent_decode(std::string_view uri, const std::string& where)
{
    std::string out;
    for (std::size_t i = 0; i < uri.size(); ++i) {
        if (uri[i] != '%') {
            out.push_back(uri[i]);
            continue;
        }
        unsigned value = 0;
        for (std::size_t digit = 1; digit <= 2; ++digit) {
            const char c = i + digit < uri.size() ? uri[i + digit] : '\0';
            const int nibble = (c >= '0' && c <= '9') ? c - '0'
                : (c >= 'a' && c <= 'f')              ? c - 'a' + 10
                : (c >= 'A' && c <= 'F')              ? c - 'A' + 10
                                                      : -1;
            if (nibble < 0) {
                refuse(where + ": the uri has a malformed %-escape");
            }
            value = value * 16U + static_cast<unsigned>(nibble);
        }
        out.push_back(static_cast<char>(value));
        i += 2;
    }
    return out;
}

constexpr std::uint32_t component_float = 5126;
constexpr std::uint32_t component_u8 = 5121;
constexpr std::uint32_t component_u16 = 5123;
constexpr std::uint32_t component_u32 = 5125;

struct View {
    std::span<const std::byte> bytes;
    std::size_t stride = 0; // 0: tightly packed
};

struct Accessor {
    std::size_t count = 0;
    std::uint32_t component = 0;
    std::string type;
    std::optional<std::size_t> view;
    std::size_t offset = 0;
};

class Model {
public:
    Model(const Json& root, std::optional<std::span<const std::byte>> glb_binary, const BufferReader& external)
        : root_(root)
        , glb_binary_(glb_binary)
        , external_(external)
    {
    }

    std::vector<math::Vec3> vec3s(std::size_t index, std::string_view purpose)
    {
        const Accessor a = accessor(index);
        const std::string where = name("accessor", index);
        if (a.component != component_float || a.type != "VEC3") {
            refuse(where + ": " + std::string{purpose} + " must be float VEC3");
        }
        if (a.count > mesh_max_vertices) {
            refuse(where + ": more than " + std::to_string(mesh_max_vertices) + " vertices");
        }
        if (!a.view) {
            return std::vector<math::Vec3>(a.count); // an accessor without a buffer view holds zeros
        }
        // Bounds first, so a small file cannot make the reader allocate for data it lacks.
        const View v = view(*a.view);
        const std::span<const std::byte> bytes = elements(v, a, 12, where);
        const std::size_t stride = v.stride != 0 ? v.stride : 12;
        std::vector<math::Vec3> out(a.count);
        for (std::size_t i = 0; i < a.count; ++i) {
            float xyz[3];
            std::memcpy(xyz, bytes.data() + i * stride, sizeof(xyz));
            if (!std::isfinite(xyz[0]) || !std::isfinite(xyz[1]) || !std::isfinite(xyz[2])) {
                refuse(where + " has a value that is not finite");
            }
            out[i] = {xyz[0], xyz[1], xyz[2]};
        }
        return out;
    }

    std::vector<std::uint32_t> indices(std::size_t index)
    {
        const Accessor a = accessor(index);
        const std::string where = name("accessor", index);
        const std::size_t size = a.component == component_u8 ? 1
            : a.component == component_u16                  ? 2
            : a.component == component_u32                  ? 4
                                                            : 0;
        if (size == 0 || a.type != "SCALAR") {
            refuse(where + ": indices must be unsigned 8-, 16- or 32-bit SCALAR");
        }
        if (a.count > mesh_max_indices) {
            refuse(where + ": more than " + std::to_string(mesh_max_indices) + " indices");
        }
        if (!a.view) {
            return std::vector<std::uint32_t>(a.count);
        }
        // Index data is tightly packed; a stride on its buffer view is not used.
        View v = view(*a.view);
        v.stride = 0;
        const std::span<const std::byte> bytes = elements(v, a, size, where);
        std::vector<std::uint32_t> out(a.count);
        for (std::size_t i = 0; i < a.count; ++i) {
            std::uint32_t value = 0;
            for (std::size_t b = 0; b < size; ++b) {
                value |= static_cast<std::uint32_t>(bytes[i * size + b]) << (8U * b);
            }
            out[i] = value;
        }
        return out;
    }

private:
    Accessor accessor(std::size_t index)
    {
        const Json& object = element(array_field(root_, "accessors", "the model"), index, "accessor");
        const std::string where = name("accessor", index);
        if (member(object, "sparse") != nullptr) {
            refuse(where + " is sparse, which is not supported yet");
        }
        Accessor a;
        a.count = required_index(object, "count", where);
        a.component = static_cast<std::uint32_t>(required_index(object, "componentType", where));
        const Json* type = member(object, "type");
        if (type == nullptr || !type->is_string()) {
            refuse(where + " has no type");
        }
        a.type = type->get<std::string>();
        a.view = optional_index(object, "bufferView", where);
        a.offset = optional_index(object, "byteOffset", where).value_or(0);
        if (a.count == 0) {
            refuse(where + " has no elements");
        }
        return a;
    }

    // The bytes an accessor's elements occupy, checked to lie inside its buffer view.
    static std::span<const std::byte> elements(const View& v, const Accessor& a, std::size_t size, const std::string& where)
    {
        const std::size_t stride = v.stride != 0 ? v.stride : size;
        if (size == 0 || stride < size) {
            refuse(where + ": the buffer view's stride is smaller than one element");
        }
        const std::size_t available = a.offset <= v.bytes.size() ? v.bytes.size() - a.offset : 0;
        if (available < size || (a.count - 1) > (available - size) / stride) {
            refuse(where + " runs past the end of its buffer view");
        }
        return v.bytes.subspan(a.offset);
    }

    View view(std::size_t index)
    {
        const Json& object = element(array_field(root_, "bufferViews", "the model"), index, "buffer view");
        const std::string where = name("buffer view", index);
        const std::size_t buffer_index = required_index(object, "buffer", where);
        const std::size_t offset = optional_index(object, "byteOffset", where).value_or(0);
        const std::size_t length = required_index(object, "byteLength", where);
        const std::size_t stride = optional_index(object, "byteStride", where).value_or(0);
        if (stride != 0 && (stride < 4 || stride > 252 || stride % 4 != 0)) {
            refuse(where + ": byteStride must be a multiple of 4 from 4 to 252");
        }
        const std::span<const std::byte> data = buffer(buffer_index);
        if (offset > data.size() || length > data.size() - offset) {
            refuse(where + " runs past the end of buffer " + std::to_string(buffer_index));
        }
        return {data.subspan(offset, length), stride};
    }

    std::span<const std::byte> buffer(std::size_t index)
    {
        const Json& object = element(array_field(root_, "buffers", "the model"), index, "buffer");
        const std::string where = name("buffer", index);
        const std::size_t length = required_index(object, "byteLength", where);
        const Json* uri = member(object, "uri");
        std::span<const std::byte> data;
        if (uri == nullptr) {
            // Only the first buffer of a .glb may live in its binary chunk.
            if (index != 0 || !glb_binary_) {
                refuse(where + " has no uri and no .glb binary chunk");
            }
            data = *glb_binary_;
        } else {
            if (!uri->is_string()) {
                refuse(where + ": uri must be a string");
            }
            const auto cached = loaded_.find(index);
            if (cached == loaded_.end()) {
                loaded_.emplace(index, fetch(uri->get<std::string>(), where));
            }
            data = loaded_.at(index);
        }
        if (data.size() < length) {
            refuse(where + " is shorter than its byteLength");
        }
        return data.first(length);
    }

    std::vector<std::byte> fetch(const std::string& uri, const std::string& where)
    {
        if (uri.starts_with("data:")) {
            const std::size_t comma = uri.find(";base64,");
            if (comma == std::string::npos) {
                refuse(where + ": only base64 data URIs are supported");
            }
            return decode_base64(std::string_view{uri}.substr(comma + 8), where);
        }
        if (!external_) {
            refuse(where + " is in another file (" + uri + "), which this read cannot reach");
        }
        auto data = external_(percent_decode(uri, where));
        if (!data) {
            refuse(where + ": could not read " + uri + " (missing, unreadable, or outside the model's folder)");
        }
        return std::move(*data);
    }

    const Json& root_;
    std::optional<std::span<const std::byte>> glb_binary_;
    const BufferReader& external_;
    std::map<std::size_t, std::vector<std::byte>> loaded_;
};

// --- Scene ----------------------------------------------------------------------------------

math::Mat4 node_matrix(const Json& node, const std::string& where)
{
    const Json* matrix = member(node, "matrix");
    if (matrix != nullptr) {
        if (member(node, "translation") != nullptr || member(node, "rotation") != nullptr || member(node, "scale") != nullptr) {
            refuse(where + " has both a matrix and translation, rotation or scale");
        }
        const std::array<float, 16> values = numbers<16>(*matrix, where + ".matrix");
        math::Mat4 m;
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                m.m[column][row] = values[static_cast<std::size_t>(column * 4 + row)];
            }
        }
        return m;
    }
    math::Vec3 t{};
    math::Quat r{};
    math::Vec3 s{1.F, 1.F, 1.F};
    if (const Json* value = member(node, "translation")) {
        const auto v = numbers<3>(*value, where + ".translation");
        t = {v[0], v[1], v[2]};
    }
    if (const Json* value = member(node, "rotation")) {
        const auto v = numbers<4>(*value, where + ".rotation");
        r = {v[0], v[1], v[2], v[3]};
        if (r.x == 0.F && r.y == 0.F && r.z == 0.F && r.w == 0.F) {
            refuse(where + ".rotation is zero");
        }
        r = math::normalize(r);
    }
    if (const Json* value = member(node, "scale")) {
        const auto v = numbers<3>(*value, where + ".scale");
        s = {v[0], v[1], v[2]};
    }
    return math::trs(t, r, s);
}

math::Vec3 column(const math::Mat4& m, int c)
{
    return {m.m[c][0], m.m[c][1], m.m[c][2]};
}

math::Vec3 unit_or_up(math::Vec3 v)
{
    const math::Vec3 n = math::normalize(v);
    return n == math::Vec3{} || !std::isfinite(n.x) || !std::isfinite(n.y) || !std::isfinite(n.z) ? math::Vec3{0.F, 1.F, 0.F} : n;
}

// Appends one primitive's triangles, transformed by `world`, to `out`.
void add_primitive(Model& model, const Json& primitive, const math::Mat4& world, const std::string& where, MeshData& out)
{
    const std::size_t mode = optional_index(primitive, "mode", where).value_or(4);
    if (mode < 4) {
        return; // points and lines have no surface
    }
    if (mode > 6) {
        refuse(where + ": unknown primitive mode " + std::to_string(mode));
    }
    const Json* attributes = member(primitive, "attributes");
    if (attributes == nullptr || !attributes->is_object()) {
        refuse(where + " has no attributes");
    }
    const auto position_accessor = optional_index(*attributes, "POSITION", where + ".attributes");
    if (!position_accessor) {
        return; // the spec says to skip a primitive without positions
    }
    const std::vector<math::Vec3> positions = model.vec3s(*position_accessor, "POSITION");
    std::vector<math::Vec3> normals;
    if (const auto normal_accessor = optional_index(*attributes, "NORMAL", where + ".attributes")) {
        normals = model.vec3s(*normal_accessor, "NORMAL");
        if (normals.size() != positions.size()) {
            refuse(where + ": NORMAL and POSITION have different counts");
        }
    }
    std::vector<std::uint32_t> order;
    if (const auto index_accessor = optional_index(primitive, "indices", where)) {
        order = model.indices(*index_accessor);
    } else {
        order.resize(positions.size());
        for (std::size_t i = 0; i < order.size(); ++i) {
            order[i] = static_cast<std::uint32_t>(i);
        }
    }
    if (std::any_of(order.begin(), order.end(), [&](std::uint32_t i) { return i >= positions.size(); })) {
        refuse(where + ": an index is past the last vertex");
    }

    // Triangle lists as they are; strips and fans unrolled as the glTF spec defines them.
    std::vector<std::uint32_t> triangles;
    if (mode == 4) {
        if (order.size() % 3 != 0) {
            refuse(where + ": a triangle list's index count is not a multiple of 3");
        }
        triangles = std::move(order);
    } else if (order.size() >= 3) {
        triangles.reserve((order.size() - 2) * 3);
        for (std::size_t i = 0; i + 2 < order.size(); ++i) {
            if (mode == 5) {
                const std::size_t odd = i % 2;
                triangles.insert(triangles.end(), {order[i], order[i + 1 + odd], order[i + 2 - odd]});
            } else {
                triangles.insert(triangles.end(), {order[i + 1], order[i + 2], order[0]});
            }
        }
    }

    // Normals go through the inverse transpose of the transform's 3x3 part. Its columns are the
    // cross products of the transform's columns divided by the determinant; dividing by the
    // determinant's sign is enough because normals are made unit length after. A negative
    // determinant (a mirror) also reverses each triangle so it still faces outwards.
    const math::Vec3 a = column(world, 0);
    const math::Vec3 b = column(world, 1);
    const math::Vec3 c = column(world, 2);
    const float determinant = math::dot(a, math::cross(b, c));
    if (determinant == 0.F || !std::isfinite(determinant)) {
        return; // a flattened transform leaves no surface
    }
    const float sign = determinant < 0.F ? -1.F : 1.F;
    const math::Vec3 nx = math::cross(b, c) * sign;
    const math::Vec3 ny = math::cross(c, a) * sign;
    const math::Vec3 nz = math::cross(a, b) * sign;
    const bool mirrored = determinant < 0.F;

    const std::size_t new_vertices = normals.empty() ? triangles.size() : positions.size();
    if (out.positions.size() + new_vertices > mesh_max_vertices || out.indices.size() + triangles.size() > mesh_max_indices) {
        refuse("the model is larger than " + std::to_string(mesh_max_vertices) + " vertices or "
               + std::to_string(mesh_max_indices) + " indices");
    }
    const auto base = static_cast<std::uint32_t>(out.positions.size());
    if (normals.empty()) {
        // Flat normals, as the spec requires when a primitive has none: each triangle gets its
        // own three vertices with the face's normal.
        for (std::size_t t = 0; t < triangles.size(); t += 3) {
            const math::Vec3 p0 = math::transform_point(world, positions[triangles[t]]);
            math::Vec3 p1 = math::transform_point(world, positions[triangles[t + 1]]);
            math::Vec3 p2 = math::transform_point(world, positions[triangles[t + 2]]);
            if (mirrored) {
                std::swap(p1, p2);
            }
            const math::Vec3 normal = unit_or_up(math::cross(p1 - p0, p2 - p0));
            for (const math::Vec3 p : {p0, p1, p2}) {
                out.indices.push_back(static_cast<std::uint32_t>(out.positions.size()));
                out.positions.push_back(p);
                out.normals.push_back(normal);
            }
        }
        return;
    }
    for (std::size_t i = 0; i < positions.size(); ++i) {
        out.positions.push_back(math::transform_point(world, positions[i]));
        const math::Vec3 n = normals[i];
        out.normals.push_back(unit_or_up(nx * n.x + ny * n.y + nz * n.z));
    }
    for (std::size_t t = 0; t < triangles.size(); t += 3) {
        out.indices.push_back(base + triangles[t]);
        out.indices.push_back(base + triangles[mirrored ? t + 2 : t + 1]);
        out.indices.push_back(base + triangles[mirrored ? t + 1 : t + 2]);
    }
}

MeshData read_scene(const Json& root, Model& model)
{
    if (!root.is_object()) {
        refuse("the glTF JSON is not an object");
    }
    const Json* asset = member(root, "asset");
    const Json* version = asset != nullptr && asset->is_object() ? member(*asset, "version") : nullptr;
    if (version == nullptr || !version->is_string()) {
        refuse("the model has no asset.version");
    }
    if (!version->get<std::string>().starts_with("2.")) {
        refuse("glTF " + version->get<std::string>() + " is not supported; only 2.x is read");
    }
    const Json& required = array_field(root, "extensionsRequired", "the model");
    if (!required.empty()) {
        refuse("the model requires the extension " + (required[0].is_string() ? required[0].get<std::string>() : "?")
               + ", which Lykoware does not read yet");
    }

    const Json& scenes = array_field(root, "scenes", "the model");
    if (scenes.empty()) {
        refuse("the model has no scene");
    }
    const std::size_t scene_index = optional_index(root, "scene", "the model").value_or(0);
    const Json& scene = element(scenes, scene_index, "scene");
    const Json& roots = array_field(scene, "nodes", name("scene", scene_index));
    const Json& nodes = array_field(root, "nodes", "the model");
    const Json& meshes = array_field(root, "meshes", "the model");

    // Depth first, in document order, with an explicit stack: deep hierarchies cannot overflow
    // the call stack, and a node reached twice (a cycle or two parents) is refused.
    MeshData out;
    std::vector<bool> visited(nodes.size(), false);
    std::vector<std::pair<std::size_t, math::Mat4>> stack;
    for (std::size_t i = roots.size(); i-- > 0;) {
        if (!roots[i].is_number_unsigned()) {
            refuse(name("scene", scene_index) + ": nodes must be node indices");
        }
        stack.emplace_back(static_cast<std::size_t>(roots[i].get<std::uint64_t>()), math::Mat4{});
    }
    while (!stack.empty()) {
        const auto [index, parent] = stack.back();
        stack.pop_back();
        const Json& node = element(nodes, index, "node");
        const std::string where = name("node", index);
        if (visited[index]) {
            refuse("the node graph is not a tree: " + where + " is reached twice");
        }
        visited[index] = true;
        const math::Mat4 world = parent * node_matrix(node, where);
        if (const auto mesh_index = optional_index(node, "mesh", where)) {
            const Json& mesh = element(meshes, *mesh_index, "mesh");
            const Json& primitives = array_field(mesh, "primitives", name("mesh", *mesh_index));
            for (std::size_t p = 0; p < primitives.size(); ++p) {
                const Json& primitive = element(primitives, p, name("mesh", *mesh_index) + " primitive");
                add_primitive(model, primitive, world, name("mesh", *mesh_index) + " primitive " + std::to_string(p), out);
            }
        }
        const Json& children = array_field(node, "children", where);
        for (std::size_t i = children.size(); i-- > 0;) {
            if (!children[i].is_number_unsigned()) {
                refuse(where + ": children must be node indices");
            }
            stack.emplace_back(static_cast<std::size_t>(children[i].get<std::uint64_t>()), world);
        }
    }
    if (out.indices.empty()) {
        refuse("the model's scene has no triangles");
    }
    return out;
}

bool read_file(const std::filesystem::path& path, std::vector<std::byte>& bytes, std::string& error)
{
    std::error_code code;
    const std::uintmax_t size = std::filesystem::file_size(path, code);
    if (code) {
        error = "cannot read " + path.string() + ": " + code.message();
        return false;
    }
    if (size > max_file_bytes) {
        error = path.string() + " is larger than 1 GiB";
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    bytes.resize(static_cast<std::size_t>(size));
    if (!in || !in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        error = "cannot read " + path.string();
        return false;
    }
    return true;
}

// A URI naming a file in the model's folder or below: relative, no "..", no scheme or drive.
bool inside_folder(std::string_view uri)
{
    if (uri.empty() || uri.front() == '/' || uri.find(':') != std::string_view::npos
        || uri.find('\\') != std::string_view::npos) {
        return false;
    }
    std::size_t start = 0;
    while (start <= uri.size()) {
        const std::size_t end = std::min(uri.find('/', start), uri.size());
        if (uri.substr(start, end - start) == "..") {
            return false;
        }
        start = end + 1;
    }
    return true;
}

}

std::optional<MeshData> read(std::span<const std::byte> file, const BufferReader& external, std::string& error)
{
    try {
        std::span<const std::byte> json = file;
        std::optional<std::span<const std::byte>> binary;
        if (file.size() >= 4 && std::memcmp(file.data(), "glTF", 4) == 0) {
            // .glb: a 12-byte header, then a JSON chunk and an optional binary chunk, each with
            // an 8-byte length-and-type header.
            if (file.size() < 20) {
                refuse("the .glb file is too short");
            }
            if (u32_at(file, 4) != 2) {
                refuse("only .glb version 2 is read");
            }
            const std::size_t length = u32_at(file, 8);
            if (length > file.size() || length < 20) {
                refuse("the .glb header's length does not match the file");
            }
            const std::size_t json_length = u32_at(file, 12);
            if (u32_at(file, 16) != 0x4E4F534AU || json_length > length - 20) {
                refuse("the .glb file's first chunk is not a whole JSON chunk");
            }
            json = file.subspan(20, json_length);
            const std::size_t next = 20 + json_length;
            if (next + 8 <= length && u32_at(file, next + 4) == 0x004E4942U) {
                const std::size_t binary_length = u32_at(file, next);
                if (binary_length > length - next - 8) {
                    refuse("the .glb binary chunk runs past the end of the file");
                }
                binary = file.subspan(next + 8, binary_length);
            }
        }
        const auto* text = reinterpret_cast<const char*>(json.data());
        const Json root = Json::parse(text, text + json.size(), nullptr, false);
        if (root.is_discarded()) {
            refuse("the glTF JSON is malformed");
        }
        Model model(root, binary, external);
        MeshData mesh = read_scene(root, model);
        if (!valid_mesh(mesh, error)) {
            return std::nullopt;
        }
        error.clear();
        return mesh;
    } catch (const Refusal& refusal) {
        error = refusal.reason;
    } catch (const Json::exception&) {
        error = "the glTF JSON has a value of an unexpected type";
    }
    return std::nullopt;
}

std::optional<MeshData> load(const std::filesystem::path& file, std::string& error)
{
    std::vector<std::byte> bytes;
    if (!read_file(file, bytes, error)) {
        return std::nullopt;
    }
    const std::filesystem::path folder = file.parent_path();
    const BufferReader external = [&folder](std::string_view uri) -> std::optional<std::vector<std::byte>> {
        if (!inside_folder(uri)) {
            return std::nullopt;
        }
        std::vector<std::byte> data;
        std::string ignored;
        if (!read_file(folder / std::filesystem::path(std::u8string(uri.begin(), uri.end())), data, ignored)) {
            return std::nullopt;
        }
        return data;
    };
    return read(bytes, external, error);
}

}
