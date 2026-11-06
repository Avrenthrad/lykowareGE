#pragma once

// Builds glTF 2.0 models in memory for tests (ADR 0035, ADR 0050), so the repository holds no
// model files. Binary data, buffer views and accessors are added by calls; meshes, nodes, scenes
// and other members are written as JSON by the test.

#include <lykoware/math.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace gltf_test {

using lykoware::math::Vec3;

inline std::string base64(const std::vector<std::byte>& data)
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

inline std::vector<std::byte> bytes_of(std::string_view text)
{
    std::vector<std::byte> out(text.size());
    std::memcpy(out.data(), text.data(), text.size());
    return out;
}

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

}
