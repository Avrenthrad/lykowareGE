#pragma once

// Test support: a minimal PNG writer (8-bit RGBA, uncompressed "stored" zlib blocks) and base64,
// so tests can make the images they read instead of committing image files. Header-only.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lykoware::test {

inline void put_byte(std::vector<std::byte>& out, unsigned value)
{
    out.push_back(static_cast<std::byte>(value & 0xFFU));
}

inline void put32be(std::vector<std::byte>& out, std::uint32_t v)
{
    put_byte(out, v >> 24U);
    put_byte(out, v >> 16U);
    put_byte(out, v >> 8U);
    put_byte(out, v);
}

inline std::uint32_t crc32(const std::vector<std::byte>& data, std::size_t from)
{
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t i = from; i < data.size(); ++i) {
        crc ^= static_cast<std::uint32_t>(data[i]);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1U) != 0 ? (crc >> 1U) ^ 0xEDB88320U : crc >> 1U;
        }
    }
    return ~crc;
}

inline void png_chunk(std::vector<std::byte>& out, const char* type, const std::vector<std::byte>& data)
{
    put32be(out, static_cast<std::uint32_t>(data.size()));
    const std::size_t start = out.size();
    for (int i = 0; i < 4; ++i) {
        put_byte(out, static_cast<unsigned char>(type[i]));
    }
    out.insert(out.end(), data.begin(), data.end());
    put32be(out, crc32(out, start));
}

// An 8-bit RGBA PNG of width x height; `rgba` holds the pixels row by row from the top.
inline std::vector<std::byte> png_rgba(std::uint32_t width, std::uint32_t height, const std::vector<std::uint8_t>& rgba)
{
    std::vector<std::byte> out;
    for (const unsigned b : {0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU}) {
        put_byte(out, b);
    }
    std::vector<std::byte> header;
    put32be(header, width);
    put32be(header, height);
    for (const unsigned b : {8U, 6U, 0U, 0U, 0U}) {
        put_byte(header, b);
    }
    png_chunk(out, "IHDR", header);
    std::vector<std::byte> rows;
    for (std::uint32_t y = 0; y < height; ++y) {
        put_byte(rows, 0); // no filter
        for (std::uint32_t x = 0; x < width * 4; ++x) {
            put_byte(rows, rgba[y * width * 4 + x]);
        }
    }
    std::vector<std::byte> z;
    put_byte(z, 0x78);
    put_byte(z, 0x01);
    put_byte(z, 0x01); // one final stored block
    const auto length = static_cast<std::uint32_t>(rows.size());
    put_byte(z, length);
    put_byte(z, length >> 8U);
    put_byte(z, ~length);
    put_byte(z, ~length >> 8U);
    z.insert(z.end(), rows.begin(), rows.end());
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (const std::byte byte : rows) {
        a = (a + static_cast<std::uint32_t>(byte)) % 65521U;
        b = (b + a) % 65521U;
    }
    put32be(z, (b << 16U) | a);
    png_chunk(out, "IDAT", z);
    png_chunk(out, "IEND", {});
    return out;
}

inline std::string base64(const std::vector<std::byte>& data)
{
    constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (std::size_t i = 0; i < data.size(); i += 3) {
        std::uint32_t bits = static_cast<std::uint32_t>(data[i]) << 16U;
        bits |= i + 1 < data.size() ? static_cast<std::uint32_t>(data[i + 1]) << 8U : 0U;
        bits |= i + 2 < data.size() ? static_cast<std::uint32_t>(data[i + 2]) : 0U;
        out += alphabet[(bits >> 18U) & 63U];
        out += alphabet[(bits >> 12U) & 63U];
        out += i + 1 < data.size() ? alphabet[(bits >> 6U) & 63U] : '=';
        out += i + 2 < data.size() ? alphabet[bits & 63U] : '=';
    }
    return out;
}

}
