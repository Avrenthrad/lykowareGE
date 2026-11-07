#include <lykoware/gltf.hpp>
#include <lykoware/image_decode.hpp>

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
constexpr std::uint32_t component_s8 = 5120;
constexpr std::uint32_t component_u8 = 5121;
constexpr std::uint32_t component_s16 = 5122;
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
    bool normalized = false;
};

class Document {
public:
    Document(const Json& root, std::optional<std::span<const std::byte>> glb_binary, const BufferReader& external)
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

    // Texture coordinates: float, or normalised unsigned 8- or 16-bit, VEC2 (the glTF spec's
    // allowed forms for TEXCOORD_n).
    std::vector<math::Vec2> vec2s(std::size_t index, std::string_view purpose)
    {
        const Accessor a = accessor(index);
        const std::string where = name("accessor", index);
        const std::size_t size = a.component == component_float ? 4
            : a.component == component_u8                     ? 1
            : a.component == component_u16                    ? 2
                                                              : 0;
        if (size == 0 || a.type != "VEC2" || (size != 4 && !a.normalized)) {
            refuse(where + ": " + std::string{purpose} + " must be float, or normalised unsigned 8- or 16-bit, VEC2");
        }
        if (a.count > mesh_max_vertices) {
            refuse(where + ": more than " + std::to_string(mesh_max_vertices) + " vertices");
        }
        if (!a.view) {
            return std::vector<math::Vec2>(a.count);
        }
        const View v = view(*a.view);
        const std::span<const std::byte> bytes = elements(v, a, 2 * size, where);
        const std::size_t stride = v.stride != 0 ? v.stride : 2 * size;
        std::vector<math::Vec2> out(a.count);
        for (std::size_t i = 0; i < a.count; ++i) {
            float uv[2] = {};
            for (std::size_t c = 0; c < 2; ++c) {
                const std::byte* at = bytes.data() + i * stride + c * size;
                if (size == 4) {
                    std::memcpy(&uv[c], at, 4);
                } else if (size == 1) {
                    uv[c] = static_cast<float>(std::to_integer<unsigned>(at[0])) / 255.F;
                } else {
                    uv[c] = static_cast<float>(std::to_integer<unsigned>(at[0]) | (std::to_integer<unsigned>(at[1]) << 8U)) / 65535.F;
                }
            }
            if (!std::isfinite(uv[0]) || !std::isfinite(uv[1])) {
                refuse(where + " has a value that is not finite");
            }
            out[i] = {uv[0], uv[1]};
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

    // `width` components an element, as floats: float, or with `normalised` also the normalised 8-
    // and 16-bit integer forms glTF allows for weights and rotation keys, signed or not.
    std::vector<float> components(std::size_t index, std::string_view type, std::size_t width, bool normalised,
                                  std::string_view purpose)
    {
        const Accessor a = accessor(index);
        const std::string where = name("accessor", index);
        const std::size_t size = a.component == component_float                         ? 4
            : a.component == component_s8 || a.component == component_u8   ? 1
            : a.component == component_s16 || a.component == component_u16 ? 2
                                                                           : 0;
        if (size == 0 || a.type != type || (size != 4 && !(normalised && a.normalized))) {
            refuse(where + ": " + std::string{purpose} + " must be float"
                   + (normalised ? std::string{", or normalised 8- or 16-bit integers,"} : std::string{}) + " "
                   + std::string{type});
        }
        if (a.count > mesh_max_vertices) {
            refuse(where + ": more than " + std::to_string(mesh_max_vertices) + " elements");
        }
        std::vector<float> out(a.count * width);
        if (!a.view) {
            return out;
        }
        const View v = view(*a.view);
        const std::span<const std::byte> bytes = elements(v, a, width * size, where);
        const std::size_t stride = v.stride != 0 ? v.stride : width * size;
        for (std::size_t i = 0; i < a.count; ++i) {
            for (std::size_t c = 0; c < width; ++c) {
                const std::byte* at = bytes.data() + i * stride + c * size;
                float value = 0.F;
                if (size == 4) {
                    std::memcpy(&value, at, 4);
                } else {
                    const unsigned raw = size == 1 ? std::to_integer<unsigned>(at[0])
                                                   : std::to_integer<unsigned>(at[0]) | (std::to_integer<unsigned>(at[1]) << 8U);
                    if (a.component == component_u8) {
                        value = static_cast<float>(raw) / 255.F;
                    } else if (a.component == component_u16) {
                        value = static_cast<float>(raw) / 65535.F;
                    } else if (a.component == component_s8) {
                        value = std::max(static_cast<float>(static_cast<std::int8_t>(raw)) / 127.F, -1.F);
                    } else {
                        value = std::max(static_cast<float>(static_cast<std::int16_t>(raw)) / 32767.F, -1.F);
                    }
                }
                if (!std::isfinite(value)) {
                    refuse(where + " has a value that is not finite");
                }
                out[i * width + c] = value;
            }
        }
        return out;
    }

    // JOINTS_0: unsigned 8- or 16-bit VEC4, not normalised.
    std::vector<std::array<std::uint16_t, 4>> joint_sets(std::size_t index)
    {
        const Accessor a = accessor(index);
        const std::string where = name("accessor", index);
        const std::size_t size = a.component == component_u8 ? 1 : a.component == component_u16 ? 2 : 0;
        if (size == 0 || a.type != "VEC4" || a.normalized) {
            refuse(where + ": JOINTS_0 must be unsigned 8- or 16-bit VEC4");
        }
        if (a.count > mesh_max_vertices) {
            refuse(where + ": more than " + std::to_string(mesh_max_vertices) + " vertices");
        }
        std::vector<std::array<std::uint16_t, 4>> out(a.count);
        if (!a.view) {
            return out;
        }
        const View v = view(*a.view);
        const std::span<const std::byte> bytes = elements(v, a, 4 * size, where);
        const std::size_t stride = v.stride != 0 ? v.stride : 4 * size;
        for (std::size_t i = 0; i < a.count; ++i) {
            for (std::size_t c = 0; c < 4; ++c) {
                const std::byte* at = bytes.data() + i * stride + c * size;
                const unsigned raw = size == 1 ? std::to_integer<unsigned>(at[0])
                                               : std::to_integer<unsigned>(at[0]) | (std::to_integer<unsigned>(at[1]) << 8U);
                out[i][c] = static_cast<std::uint16_t>(raw);
            }
        }
        return out;
    }

    // Float MAT4s, column by column as glTF stores them.
    std::vector<math::Mat4> mat4s(std::size_t index, std::string_view purpose)
    {
        const std::vector<float> values = components(index, "MAT4", 16, false, purpose);
        std::vector<math::Mat4> out(values.size() / 16);
        for (std::size_t i = 0; i < out.size(); ++i) {
            for (std::size_t column = 0; column < 4; ++column) {
                for (std::size_t row = 0; row < 4; ++row) {
                    out[i].m[column][row] = values[i * 16 + column * 4 + row];
                }
            }
        }
        return out;
    }

    // The bytes of a buffer view (images can live in one).
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

    // The bytes a URI names: a base64 data URI, or a file read through the external reader.
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
        if (const Json* normalized = member(object, "normalized")) {
            if (!normalized->is_boolean()) {
                refuse(where + ": normalized must be true or false");
            }
            a.normalized = normalized->get<bool>();
        }
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

// The shape being built: the merged mesh and one part per primitive.
struct Built {
    MeshData mesh;
    std::vector<Part> parts;
    // Skinned models only: each vertex's skeleton joints and weights.
    std::vector<std::array<std::uint16_t, 4>> joints;
    std::vector<math::Vec4> weights;
};

constexpr std::size_t gl_nearest = 9728;
constexpr std::size_t gl_linear = 9729;
constexpr std::size_t gl_clamp_to_edge = 33071;
constexpr std::size_t gl_mirrored_repeat = 33648;
constexpr std::size_t gl_repeat = 10497;

// Turns the glTF materials, textures and images the parts use into a Model's lists, each once
// and in first-use order. Images are decoded here.
class Materials {
public:
    Materials(const Json& root, Document& document, Model& out)
        : root_(root)
        , document_(document)
        , out_(out)
    {
    }

    std::size_t material(std::size_t index)
    {
        if (const auto found = materials_.find(index); found != materials_.end()) {
            return found->second;
        }
        const Json& object = element(array_field(root_, "materials", "the model"), index, "material");
        const std::string where = name("material", index);
        Material resolved;
        if (const Json* pbr = member(object, "pbrMetallicRoughness")) {
            if (!pbr->is_object()) {
                refuse(where + ": pbrMetallicRoughness must be an object");
            }
            if (const Json* factor = member(*pbr, "baseColorFactor")) {
                const auto v = numbers<4>(*factor, where + ".baseColorFactor");
                if (std::any_of(v.begin(), v.end(), [](float c) { return c < 0.F || c > 1.F; })) {
                    refuse(where + ": baseColorFactor values must be 0..1");
                }
                resolved.base_colour = {v[0], v[1], v[2], v[3]};
            }
            if (const Json* info = member(*pbr, "baseColorTexture")) {
                if (!info->is_object()) {
                    refuse(where + ": baseColorTexture must be an object");
                }
                if (optional_index(*info, "texCoord", where + ".baseColorTexture").value_or(0) != 0) {
                    refuse(where + ": the base colour texture uses a texture coordinate set other than 0, which is not read yet");
                }
                resolved.base_colour_texture = texture(required_index(*info, "index", where + ".baseColorTexture"));
            }
        }
        out_.materials.push_back(resolved);
        materials_.emplace(index, out_.materials.size() - 1);
        return out_.materials.size() - 1;
    }

private:
    std::size_t texture(std::size_t index)
    {
        if (const auto found = textures_.find(index); found != textures_.end()) {
            return found->second;
        }
        const Json& object = element(array_field(root_, "textures", "the model"), index, "texture");
        const std::string where = name("texture", index);
        const auto source = optional_index(object, "source", where);
        if (!source) {
            refuse(where + " has no source image");
        }
        Texture resolved;
        if (const auto sampler_index = optional_index(object, "sampler", where)) {
            const Json& sampler = element(array_field(root_, "samplers", "the model"), *sampler_index, "sampler");
            const std::string at = name("sampler", *sampler_index);
            const std::size_t wrap = optional_index(sampler, "wrapS", at).value_or(gl_repeat);
            resolved.sampling.wrap = wrap == gl_repeat           ? Wrap::Repeat
                : wrap == gl_mirrored_repeat                    ? Wrap::MirroredRepeat
                : wrap == gl_clamp_to_edge                      ? Wrap::ClampToEdge
                                                                : (refuse(at + ": unknown wrapS " + std::to_string(wrap)), Wrap::Repeat);
            if (const auto mag = optional_index(sampler, "magFilter", at)) {
                if (*mag != gl_nearest && *mag != gl_linear) {
                    refuse(at + ": unknown magFilter " + std::to_string(*mag));
                }
                resolved.sampling.nearest = *mag == gl_nearest;
            }
            if (const auto min = optional_index(sampler, "minFilter", at)) {
                if (*min != gl_nearest && *min != gl_linear && (*min < 9984 || *min > 9987)) {
                    refuse(at + ": unknown minFilter " + std::to_string(*min));
                }
                resolved.sampling.mipmaps = *min >= 9984;
            }
        }
        resolved.image = image(*source);
        out_.textures.push_back(resolved);
        textures_.emplace(index, out_.textures.size() - 1);
        return out_.textures.size() - 1;
    }

    std::size_t image(std::size_t index)
    {
        if (const auto found = images_.find(index); found != images_.end()) {
            return found->second;
        }
        const Json& object = element(array_field(root_, "images", "the model"), index, "image");
        const std::string where = name("image", index);
        std::vector<std::byte> bytes;
        if (const Json* uri = member(object, "uri")) {
            if (!uri->is_string()) {
                refuse(where + ": uri must be a string");
            }
            bytes = document_.fetch(uri->get<std::string>(), where);
        } else if (const auto view = optional_index(object, "bufferView", where)) {
            const Json* mime = member(object, "mimeType");
            if (mime == nullptr || !mime->is_string()
                || (mime->get<std::string>() != "image/png" && mime->get<std::string>() != "image/jpeg")) {
                refuse(where + ": an image in a buffer view needs mimeType image/png or image/jpeg");
            }
            const std::span<const std::byte> data = document_.view(*view).bytes;
            bytes.assign(data.begin(), data.end());
        } else {
            refuse(where + " has neither a uri nor a bufferView");
        }
        std::string reason;
        std::optional<RgbaImage> decoded = decode_image(bytes, reason);
        if (!decoded) {
            refuse(where + ": " + reason);
        }
        pixels_ += static_cast<std::uint64_t>(decoded->width) * decoded->height;
        if (pixels_ > max_model_image_pixels) {
            refuse("the model's images hold more than " + std::to_string(max_model_image_pixels) + " pixels in all");
        }
        out_.images.push_back(std::move(*decoded));
        images_.emplace(index, out_.images.size() - 1);
        return out_.images.size() - 1;
    }

    const Json& root_;
    Document& document_;
    Model& out_;
    std::map<std::size_t, std::size_t> materials_;
    std::map<std::size_t, std::size_t> textures_;
    std::map<std::size_t, std::size_t> images_;
    std::uint64_t pixels_ = 0;
};

// --- Skins and animations (ADR 0050) -------------------------------------------------------

// How a skinned primitive's joint indices (into the skin's joint list) map to the skeleton.
struct SkinBinding {
    std::size_t skin = 0;
    std::vector<std::uint16_t> skeleton_joint;
};

// The model's one skin, as a skeleton, and which skeleton joint each node is.
struct SkinSetup {
    SkinBinding binding;
    animation::Skeleton skeleton;
    std::map<std::size_t, std::uint16_t> node_joint;
};

// A node matrix as translation, rotation and scale; false when it shears, projects or has a
// zero scale. A mirror becomes a negative X scale.
bool decompose(const math::Mat4& m, animation::JointPose& pose)
{
    if (m.m[0][3] != 0.F || m.m[1][3] != 0.F || m.m[2][3] != 0.F || m.m[3][3] != 1.F) {
        return false;
    }
    math::Vec3 x = column(m, 0);
    math::Vec3 y = column(m, 1);
    math::Vec3 z = column(m, 2);
    float sx = math::length(x);
    const float sy = math::length(y);
    const float sz = math::length(z);
    if (!(sx > 0.F) || !(sy > 0.F) || !(sz > 0.F) || !std::isfinite(sx) || !std::isfinite(sy) || !std::isfinite(sz)) {
        return false;
    }
    if (math::dot(x, math::cross(y, z)) < 0.F) {
        sx = -sx;
    }
    x = x * (1.F / sx);
    y = y * (1.F / sy);
    z = z * (1.F / sz);
    if (std::abs(math::dot(x, y)) > 1e-3F || std::abs(math::dot(x, z)) > 1e-3F || std::abs(math::dot(y, z)) > 1e-3F) {
        return false;
    }
    // The rotation matrix's columns are x, y and z; R[row][column].
    const float r00 = x.x;
    const float r10 = x.y;
    const float r20 = x.z;
    const float r01 = y.x;
    const float r11 = y.y;
    const float r21 = y.z;
    const float r02 = z.x;
    const float r12 = z.y;
    const float r22 = z.z;
    math::Quat q;
    const float trace = r00 + r11 + r22;
    if (trace > 0.F) {
        const float k = std::sqrt(trace + 1.F) * 2.F;
        q = {(r21 - r12) / k, (r02 - r20) / k, (r10 - r01) / k, 0.25F * k};
    } else if (r00 > r11 && r00 > r22) {
        const float k = std::sqrt(1.F + r00 - r11 - r22) * 2.F;
        q = {0.25F * k, (r01 + r10) / k, (r02 + r20) / k, (r21 - r12) / k};
    } else if (r11 > r22) {
        const float k = std::sqrt(1.F + r11 - r00 - r22) * 2.F;
        q = {(r01 + r10) / k, 0.25F * k, (r12 + r21) / k, (r02 - r20) / k};
    } else {
        const float k = std::sqrt(1.F + r22 - r00 - r11) * 2.F;
        q = {(r02 + r20) / k, (r12 + r21) / k, 0.25F * k, (r10 - r01) / k};
    }
    pose.translation = column(m, 3);
    pose.rotation = math::normalize(q);
    pose.scale = {sx, sy, sz};
    return true;
}

// A node's own transform as translation, rotation and scale.
animation::JointPose node_pose(const Json& node, const std::string& where)
{
    animation::JointPose pose;
    if (member(node, "matrix") != nullptr) {
        if (!decompose(node_matrix(node, where), pose)) {
            refuse(where + ": a skeleton node's matrix must be a translation, rotation and scale (no shear)");
        }
        return pose;
    }
    if (const Json* value = member(node, "translation")) {
        const auto v = numbers<3>(*value, where + ".translation");
        pose.translation = {v[0], v[1], v[2]};
    }
    if (const Json* value = member(node, "rotation")) {
        const auto v = numbers<4>(*value, where + ".rotation");
        if (v[0] == 0.F && v[1] == 0.F && v[2] == 0.F && v[3] == 0.F) {
            refuse(where + ".rotation is zero");
        }
        pose.rotation = math::normalize(math::Quat{v[0], v[1], v[2], v[3]});
    }
    if (const Json* value = member(node, "scale")) {
        const auto v = numbers<3>(*value, where + ".scale");
        pose.scale = {v[0], v[1], v[2]};
    }
    return pose;
}

// Finds the one skin the model's meshes use and makes its skeleton: the skin's joints and every
// node above them, parents first (by depth, then node index).
SkinSetup prepare_skin(const Json& root, Document& document)
{
    const Json& nodes = array_field(root, "nodes", "the model");
    std::optional<std::size_t> used;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const Json& node = element(nodes, i, "node");
        if (member(node, "mesh") == nullptr) {
            continue;
        }
        if (const auto skin = optional_index(node, "skin", name("node", i))) {
            if (used && *used != *skin) {
                refuse("the model's meshes use more than one skin, which is not read yet");
            }
            used = skin;
        }
    }
    if (!used) {
        refuse("the model has no skinned mesh");
    }
    const std::string where = name("skin", *used);
    const Json& skin = element(array_field(root, "skins", "the model"), *used, "skin");
    const Json& joints = array_field(skin, "joints", where);
    if (joints.empty()) {
        refuse(where + " has no joints");
    }

    std::vector<std::optional<std::size_t>> parent(nodes.size());
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const Json& children = array_field(element(nodes, i, "node"), "children", name("node", i));
        for (const Json& child : children) {
            if (!child.is_number_unsigned() || child.get<std::uint64_t>() >= nodes.size()) {
                refuse(name("node", i) + ": children must be node indices");
            }
            const auto c = static_cast<std::size_t>(child.get<std::uint64_t>());
            if (parent[c]) {
                refuse("the node graph is not a tree: " + name("node", c) + " has two parents");
            }
            parent[c] = i;
        }
    }
    const auto depth = [&](std::size_t node) {
        std::size_t steps = 0;
        for (std::optional<std::size_t> at = parent[node]; at; at = parent[*at]) {
            if (++steps > nodes.size()) {
                refuse("the node graph has a cycle through " + name("node", node));
            }
        }
        return steps;
    };

    std::vector<std::size_t> skin_nodes;
    std::vector<bool> needed(nodes.size(), false);
    for (const Json& joint : joints) {
        if (!joint.is_number_unsigned() || joint.get<std::uint64_t>() >= nodes.size()) {
            refuse(where + ": joints must be node indices");
        }
        const auto node = static_cast<std::size_t>(joint.get<std::uint64_t>());
        if (std::find(skin_nodes.begin(), skin_nodes.end(), node) != skin_nodes.end()) {
            refuse(where + ": " + name("node", node) + " is a joint twice");
        }
        skin_nodes.push_back(node);
        (void)depth(node); // refuses a cycle before the walk below
        for (std::optional<std::size_t> at = node; at; at = parent[*at]) {
            needed[*at] = true;
        }
    }
    std::vector<std::pair<std::size_t, std::size_t>> order;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (needed[i]) {
            order.emplace_back(depth(i), i);
        }
    }
    if (order.size() > animation::max_joints) {
        refuse(where + ": its skeleton has " + std::to_string(order.size()) + " joints, more than "
               + std::to_string(animation::max_joints));
    }
    std::sort(order.begin(), order.end());

    SkinSetup setup;
    for (const auto& [level, node_index] : order) {
        (void)level;
        const Json& node = element(nodes, node_index, "node");
        animation::Joint joint;
        if (const Json* label = member(node, "name"); label != nullptr && label->is_string()) {
            joint.name = label->get<std::string>();
        }
        if (parent[node_index]) {
            joint.parent = setup.node_joint.at(*parent[node_index]);
        }
        joint.rest = node_pose(node, name("node", node_index));
        setup.node_joint.emplace(node_index, static_cast<std::uint16_t>(setup.skeleton.joints.size()));
        setup.skeleton.joints.push_back(std::move(joint));
    }
    std::vector<math::Mat4> inverse_binds;
    if (const auto accessor = optional_index(skin, "inverseBindMatrices", where)) {
        inverse_binds = document.mat4s(*accessor, "inverseBindMatrices");
        if (inverse_binds.size() < skin_nodes.size()) {
            refuse(where + ": fewer inverse bind matrices than joints");
        }
    }
    setup.binding.skin = *used;
    for (std::size_t i = 0; i < skin_nodes.size(); ++i) {
        const std::uint16_t joint = setup.node_joint.at(skin_nodes[i]);
        setup.binding.skeleton_joint.push_back(joint);
        if (!inverse_binds.empty()) {
            setup.skeleton.joints[joint].inverse_bind = inverse_binds[i];
        }
    }
    std::string error;
    if (!animation::valid_skeleton(setup.skeleton, error)) {
        refuse(where + ": " + error);
    }
    return setup;
}

// Each animation as a clip on the skeleton. Channels for nodes outside it, and morph-target
// weights, are left out.
std::vector<animation::Clip> read_clips(const Json& root, Document& document, const SkinSetup& setup)
{
    std::vector<animation::Clip> clips;
    const Json& animations = array_field(root, "animations", "the model");
    for (std::size_t a = 0; a < animations.size(); ++a) {
        const Json& source = element(animations, a, "animation");
        const std::string where = name("animation", a);
        animation::Clip clip;
        const Json* label = member(source, "name");
        clip.name = label != nullptr && label->is_string() && !label->get<std::string>().empty() ? label->get<std::string>()
                                                                                              : where;
        const Json& samplers = array_field(source, "samplers", where);
        const Json& channels = array_field(source, "channels", where);
        for (std::size_t c = 0; c < channels.size(); ++c) {
            const std::string at = where + " channel " + std::to_string(c);
            const Json& channel = element(channels, c, where + " channel");
            const Json* target = member(channel, "target");
            if (target == nullptr || !target->is_object()) {
                refuse(at + " has no target");
            }
            const Json* path = member(*target, "path");
            if (path == nullptr || !path->is_string()) {
                refuse(at + " has no target path");
            }
            const std::string kind = path->get<std::string>();
            if (kind == "weights") {
                continue; // morph targets are not read yet
            }
            animation::Channel out;
            if (kind == "translation") {
                out.target = animation::Target::Translation;
            } else if (kind == "rotation") {
                out.target = animation::Target::Rotation;
            } else if (kind == "scale") {
                out.target = animation::Target::Scale;
            } else {
                refuse(at + ": unknown target path " + kind);
            }
            const auto node = optional_index(*target, "node", at + ".target");
            if (!node) {
                continue; // allowed when an extension names the target
            }
            const auto joint = setup.node_joint.find(*node);
            if (joint == setup.node_joint.end()) {
                continue; // not part of the skeleton
            }
            out.joint = joint->second;
            const std::size_t sampler_index = required_index(channel, "sampler", at);
            const std::string sampler_where = where + " sampler " + std::to_string(sampler_index);
            const Json& sampler = element(samplers, sampler_index, where + " sampler");
            if (const Json* interpolation = member(sampler, "interpolation")) {
                const std::string mode = interpolation->is_string() ? interpolation->get<std::string>() : std::string{};
                if (mode == "LINEAR") {
                    out.interpolation = animation::Interpolation::Linear;
                } else if (mode == "STEP") {
                    out.interpolation = animation::Interpolation::Step;
                } else if (mode == "CUBICSPLINE") {
                    out.interpolation = animation::Interpolation::CubicSpline;
                } else {
                    refuse(sampler_where + ": unknown interpolation " + mode);
                }
            }
            out.times = document.components(required_index(sampler, "input", sampler_where), "SCALAR", 1, false,
                                            "an animation's input");
            const bool rotation = out.target == animation::Target::Rotation;
            const std::size_t width = rotation ? 4 : 3;
            const std::vector<float> values = document.components(required_index(sampler, "output", sampler_where),
                                                                  rotation ? "VEC4" : "VEC3", width, rotation,
                                                                  "an animation's output");
            for (std::size_t i = 0; i < values.size(); i += width) {
                out.values.push_back({values[i], values[i + 1], values[i + 2], rotation ? values[i + 3] : 0.F});
            }
            clip.channels.push_back(std::move(out));
        }
        std::string error;
        if (!animation::valid_clip(clip, setup.skeleton, error)) {
            refuse(where + ": " + error);
        }
        clips.push_back(std::move(clip));
    }
    return clips;
}

// Appends one primitive's triangles, transformed by `world`, to `built`, as one part. With
// `materials`, the primitive's material is resolved too. With `skin`, its joints and weights are
// read and mapped to the skeleton.
void add_primitive(Document& model, const Json& primitive, const math::Mat4& world, const std::string& where, Built& built,
                   Materials* materials, const SkinBinding* skin)
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
    std::vector<math::Vec2> uvs;
    if (const auto uv_accessor = optional_index(*attributes, "TEXCOORD_0", where + ".attributes")) {
        uvs = model.vec2s(*uv_accessor, "TEXCOORD_0");
        if (uvs.size() != positions.size()) {
            refuse(where + ": TEXCOORD_0 and POSITION have different counts");
        }
    }
    std::vector<math::Vec3> normals;
    if (const auto normal_accessor = optional_index(*attributes, "NORMAL", where + ".attributes")) {
        normals = model.vec3s(*normal_accessor, "NORMAL");
        if (normals.size() != positions.size()) {
            refuse(where + ": NORMAL and POSITION have different counts");
        }
    }
    std::vector<std::array<std::uint16_t, 4>> joints;
    std::vector<math::Vec4> weights;
    if (skin != nullptr) {
        const auto joints_accessor = optional_index(*attributes, "JOINTS_0", where + ".attributes");
        const auto weights_accessor = optional_index(*attributes, "WEIGHTS_0", where + ".attributes");
        if (!joints_accessor || !weights_accessor) {
            refuse(where + ": a skinned primitive needs JOINTS_0 and WEIGHTS_0");
        }
        if (member(*attributes, "JOINTS_1") != nullptr || member(*attributes, "WEIGHTS_1") != nullptr) {
            refuse(where + ": more than four joints a vertex (JOINTS_1) is not read yet");
        }
        joints = model.joint_sets(*joints_accessor);
        const std::vector<float> raw = model.components(*weights_accessor, "VEC4", 4, true, "WEIGHTS_0");
        if (joints.size() != positions.size() || raw.size() != positions.size() * 4) {
            refuse(where + ": JOINTS_0, WEIGHTS_0 and POSITION have different counts");
        }
        weights.resize(positions.size());
        for (std::size_t i = 0; i < positions.size(); ++i) {
            std::array<float, 4> w{raw[4 * i], raw[4 * i + 1], raw[4 * i + 2], raw[4 * i + 3]};
            float sum = 0.F;
            for (const float value : w) {
                if (value < 0.F) {
                    refuse(where + ": vertex " + std::to_string(i) + " has a negative weight");
                }
                sum += value;
            }
            if (!(sum > 0.F)) {
                refuse(where + ": vertex " + std::to_string(i) + "'s weights sum to 0");
            }
            for (std::size_t k = 0; k < 4; ++k) {
                if (w[k] == 0.F) {
                    joints[i][k] = 0; // unused; any index will do
                    continue;
                }
                if (joints[i][k] >= skin->skeleton_joint.size()) {
                    refuse(where + ": vertex " + std::to_string(i) + " names joint " + std::to_string(joints[i][k])
                           + ", outside the skin");
                }
                joints[i][k] = skin->skeleton_joint[joints[i][k]];
                w[k] /= sum;
            }
            weights[i] = {w[0], w[1], w[2], w[3]};
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

    MeshData& out = built.mesh;
    const std::size_t new_vertices = normals.empty() ? triangles.size() : positions.size();
    if (out.positions.size() + new_vertices > mesh_max_vertices || out.indices.size() + triangles.size() > mesh_max_indices) {
        refuse("the model is larger than " + std::to_string(mesh_max_vertices) + " vertices or "
               + std::to_string(mesh_max_indices) + " indices");
    }
    if (triangles.empty()) {
        return;
    }
    std::optional<std::size_t> material;
    if (materials != nullptr) {
        if (const auto index = optional_index(primitive, "material", where)) {
            material = materials->material(*index);
        }
    }
    // Texture coordinates are per vertex across the whole mesh: earlier vertices without them
    // read (0, 0), and so do this primitive's when it has none but others do.
    if (!uvs.empty() && out.uvs.size() < out.positions.size()) {
        out.uvs.resize(out.positions.size());
    }
    const auto first_index = static_cast<std::uint32_t>(out.indices.size());
    const auto base = static_cast<std::uint32_t>(out.positions.size());
    if (normals.empty()) {
        // Flat normals, as the spec requires when a primitive has none: each triangle gets its
        // own three vertices with the face's normal.
        for (std::size_t t = 0; t < triangles.size(); t += 3) {
            std::uint32_t corner[3] = {triangles[t], triangles[t + 1], triangles[t + 2]};
            if (mirrored) {
                std::swap(corner[1], corner[2]);
            }
            const math::Vec3 p0 = math::transform_point(world, positions[corner[0]]);
            const math::Vec3 p1 = math::transform_point(world, positions[corner[1]]);
            const math::Vec3 p2 = math::transform_point(world, positions[corner[2]]);
            const math::Vec3 normal = unit_or_up(math::cross(p1 - p0, p2 - p0));
            const math::Vec3 corners[3] = {p0, p1, p2};
            for (int k = 0; k < 3; ++k) {
                out.indices.push_back(static_cast<std::uint32_t>(out.positions.size()));
                out.positions.push_back(corners[k]);
                out.normals.push_back(normal);
                if (!uvs.empty()) {
                    out.uvs.push_back(uvs[corner[k]]);
                }
                if (skin != nullptr) {
                    built.joints.push_back(joints[corner[k]]);
                    built.weights.push_back(weights[corner[k]]);
                }
            }
        }
    } else {
        for (std::size_t i = 0; i < positions.size(); ++i) {
            out.positions.push_back(math::transform_point(world, positions[i]));
            const math::Vec3 n = normals[i];
            out.normals.push_back(unit_or_up(nx * n.x + ny * n.y + nz * n.z));
        }
        out.uvs.insert(out.uvs.end(), uvs.begin(), uvs.end());
        built.joints.insert(built.joints.end(), joints.begin(), joints.end());
        built.weights.insert(built.weights.end(), weights.begin(), weights.end());
        for (std::size_t t = 0; t < triangles.size(); t += 3) {
            out.indices.push_back(base + triangles[t]);
            out.indices.push_back(base + triangles[mirrored ? t + 2 : t + 1]);
            out.indices.push_back(base + triangles[mirrored ? t + 1 : t + 2]);
        }
    }
    if (!out.uvs.empty()) {
        out.uvs.resize(out.positions.size());
    }
    built.parts.push_back({first_index, static_cast<std::uint32_t>(out.indices.size() - first_index), material});
}

// With `skin`, every mesh in the scene must use that skin, and is read in bind space.
Built read_scene(const Json& root, Document& model, Materials* materials, const SkinBinding* skin)
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
    Built out;
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
            math::Mat4 placed = world;
            if (skin != nullptr) {
                const auto node_skin = optional_index(node, "skin", where);
                if (!node_skin || *node_skin != skin->skin) {
                    refuse(where + ": a mesh without the model's skin, which is not read yet in a skinned model");
                }
                placed = math::Mat4{}; // glTF: a skinned mesh's own node transform is ignored
            }
            const Json& mesh = element(meshes, *mesh_index, "mesh");
            const Json& primitives = array_field(mesh, "primitives", name("mesh", *mesh_index));
            for (std::size_t p = 0; p < primitives.size(); ++p) {
                const Json& primitive = element(primitives, p, name("mesh", *mesh_index) + " primitive");
                add_primitive(model, primitive, placed, name("mesh", *mesh_index) + " primitive " + std::to_string(p), out,
                              materials, skin);
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
    if (out.mesh.indices.empty()) {
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

namespace {

// A .gltf or .glb file's JSON, and the .glb binary chunk if there is one.
struct Parsed {
    Json root;
    std::optional<std::span<const std::byte>> binary;
};

Parsed parse(std::span<const std::byte> file)
{
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
    Parsed parsed{Json::parse(text, text + json.size(), nullptr, false), binary};
    if (parsed.root.is_discarded()) {
        refuse("the glTF JSON is malformed");
    }
    return parsed;
}

// Both reads: the shape, and with `with_materials` the materials and images too.
std::optional<Model> read_any(std::span<const std::byte> file, const BufferReader& external, std::string& error,
                              bool with_materials)
{
    try {
        const Parsed parsed = parse(file);
        const Json& root = parsed.root;
        Document document(root, parsed.binary, external);
        Model model;
        Materials materials(root, document, model);
        Built built = read_scene(root, document, with_materials ? &materials : nullptr, nullptr);
        if (!valid_mesh(built.mesh, error)) {
            return std::nullopt;
        }
        model.mesh = std::move(built.mesh);
        model.parts = std::move(built.parts);
        error.clear();
        return model;
    } catch (const Refusal& refusal) {
        error = refusal.reason;
    } catch (const Json::exception&) {
        error = "the glTF JSON has a value of an unexpected type";
    }
    return std::nullopt;
}


// Reads a model file and lets its external buffers and images come only from its folder.
template <typename Read>
auto load_any(const std::filesystem::path& file, std::string& error, Read read_with)
{
    std::vector<std::byte> bytes;
    if (!read_file(file, bytes, error)) {
        return decltype(read_with(bytes, BufferReader{})){};
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
    return read_with(bytes, external);
}

}

std::optional<MeshData> read(std::span<const std::byte> file, const BufferReader& external, std::string& error)
{
    std::optional<Model> model = read_any(file, external, error, false);
    return model ? std::optional<MeshData>(std::move(model->mesh)) : std::nullopt;
}

std::optional<Model> read_model(std::span<const std::byte> file, const BufferReader& external, std::string& error)
{
    return read_any(file, external, error, true);
}

std::optional<MeshData> load(const std::filesystem::path& file, std::string& error)
{
    return load_any(file, error, [&](std::span<const std::byte> bytes, const BufferReader& external) {
        return read(bytes, external, error);
    });
}

std::optional<Model> load_model(const std::filesystem::path& file, std::string& error)
{
    return load_any(file, error, [&](std::span<const std::byte> bytes, const BufferReader& external) {
        return read_model(bytes, external, error);
    });
}

std::optional<SkinnedModel> read_skinned(std::span<const std::byte> file, const BufferReader& external, std::string& error)
{
    try {
        const Parsed parsed = parse(file);
        Document document(parsed.root, parsed.binary, external);
        SkinnedModel out;
        Materials materials(parsed.root, document, out.model);
        SkinSetup setup = prepare_skin(parsed.root, document);
        Built built = read_scene(parsed.root, document, &materials, &setup.binding);
        if (!valid_mesh(built.mesh, error)) {
            return std::nullopt;
        }
        out.model.mesh = std::move(built.mesh);
        out.model.parts = std::move(built.parts);
        out.joints = std::move(built.joints);
        out.weights = std::move(built.weights);
        out.clips = read_clips(parsed.root, document, setup);
        out.skeleton = std::move(setup.skeleton);
        error.clear();
        return out;
    } catch (const Refusal& refusal) {
        error = refusal.reason;
    } catch (const Json::exception&) {
        error = "the glTF JSON has a value of an unexpected type";
    }
    return std::nullopt;
}

std::optional<SkinnedModel> load_skinned(const std::filesystem::path& file, std::string& error)
{
    return load_any(file, error, [&](std::span<const std::byte> bytes, const BufferReader& external) {
        return read_skinned(bytes, external, error);
    });
}

}
