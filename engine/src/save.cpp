#include <lykoware/save.hpp>

#include <bit>
#include <limits>
#include <set>
#include <tuple>

namespace lykoware::save {
namespace {
bool valid(Limits limits)
{
    return limits.file_bytes && limits.payload_bytes && limits.entries && limits.id_bytes;
}
std::uint32_t checksum(std::span<const std::byte> bytes)
{
    std::uint32_t crc = 0xffffffffU;
    for (const auto byte : bytes) {
        crc ^= std::to_integer<std::uint32_t>(byte);
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xedb88320U : 0U);
        }
    }
    return ~crc;
}
void number(std::vector<std::byte>& out, std::uint64_t value, unsigned width)
{
    for (unsigned i = 0; i < width; ++i) {
        out.push_back(static_cast<std::byte>((value >> (8 * i)) & 255));
    }
}
struct Reader {
    std::span<const std::byte> bytes;
    std::size_t offset = 0;
    bool number(std::uint64_t& value, unsigned width)
    {
        if (width > bytes.size() - offset) { return false; }
        value = 0;
        for (unsigned i = 0; i < width; ++i) {
            value |= static_cast<std::uint64_t>(std::to_integer<unsigned>(bytes[offset++])) << (8 * i);
        }
        return true;
    }
};
}
std::optional<std::vector<std::byte>> encode(const streaming::RegionEdits& edits,
                                            Limits limits, std::string& error)
{
    error.clear();
    if (!valid(limits) || edits.size() > limits.entries
        || edits.size() > std::numeric_limits<std::uint32_t>::max()
        || edits.bytes() > limits.payload_bytes || limits.file_bytes < 20) {
        error = "save limits exceeded or invalid";
        return {};
    }
    const auto entries = edits.snapshot();
    std::size_t total = 20;
    for (const auto& edit : entries) {
        if (edit.asset.size() > limits.id_bytes || edit.asset.size() > std::numeric_limits<std::uint32_t>::max()
            || limits.file_bytes - total < 20) { error = "save ID/file limit exceeded"; return {}; }
        total += 20;
        if (edit.asset.size() > limits.file_bytes - total) { error = "save file limit exceeded"; return {}; }
        total += edit.asset.size();
        if (edit.bytes.size() > limits.file_bytes - total) { error = "save file limit exceeded"; return {}; }
        total += edit.bytes.size();
    }
    std::vector<std::byte> out{std::byte{'L'}, std::byte{'Y'}, std::byte{'K'}, std::byte{'W'}};
    out.reserve(total);
    number(out, format_version, 4);
    number(out, world_version, 4);
    number(out, entries.size(), 4);
    for (const auto& edit : entries) {
        number(out, std::bit_cast<std::uint32_t>(edit.region.x), 4);
        number(out, std::bit_cast<std::uint32_t>(edit.region.z), 4);
        number(out, edit.asset.size(), 4);
        for (const unsigned char ch : edit.asset) { out.push_back(static_cast<std::byte>(ch)); }
        number(out, edit.bytes.size(), 8);
        out.insert(out.end(), edit.bytes.begin(), edit.bytes.end());
    }
    number(out, checksum(out), 4);
    return out;
}
std::optional<State> decode(std::span<const std::byte> bytes, Limits limits, std::string& error)
{
    error = "invalid, unsupported or over-limit save";
    if (!valid(limits) || bytes.size() > limits.file_bytes || bytes.size() < 16
        || bytes[0] != std::byte{'L'} || bytes[1] != std::byte{'Y'}
        || bytes[2] != std::byte{'K'} || bytes[3] != std::byte{'W'}) { return {}; }
    Reader in{bytes, 4};
    std::uint64_t version = 0, schema = 0, count = 0;
    if (!in.number(version, 4) || (version != 2 && version != format_version)) { return {}; }
    if (version == format_version) {
        if (bytes.size() < 20) { return {}; }
        Reader footer{bytes.last(4)};
        std::uint64_t expected = 0;
        if (!footer.number(expected, 4) || expected != checksum(bytes.first(bytes.size() - 4))) { return {}; }
        bytes = bytes.first(bytes.size() - 4);
        in.bytes = bytes;
    }
    if (!in.number(schema, 4) || schema != world_version || !in.number(count, 4)
        || count > limits.entries || count > (bytes.size() - in.offset) / 21) { return {}; }
    State state;
    std::set<std::tuple<std::int32_t, std::int32_t, std::string>> keys;
    std::size_t payload = 0;
    for (std::uint64_t i = 0; i < count; ++i) {
        std::uint64_t x = 0, z = 0, id_size = 0, size = 0;
        if (!in.number(x, 4) || !in.number(z, 4) || !in.number(id_size, 4)
            || id_size == 0 || id_size > limits.id_bytes || id_size > bytes.size() - in.offset) { return {}; }
        streaming::RegionEdit edit;
        edit.region = {std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(x)),
                       std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(z))};
        edit.asset.assign(reinterpret_cast<const char*>(bytes.data() + in.offset), static_cast<std::size_t>(id_size));
        in.offset += static_cast<std::size_t>(id_size);
        if (!keys.emplace(edit.region.x, edit.region.z, edit.asset).second
            || !in.number(size, 8) || size > limits.payload_bytes - payload
            || size > bytes.size() - in.offset) { return {}; }
        const auto data = bytes.subspan(in.offset, static_cast<std::size_t>(size));
        edit.bytes.assign(data.begin(), data.end());
        in.offset += static_cast<std::size_t>(size);
        payload += static_cast<std::size_t>(size);
        state.edits.push_back(std::move(edit));
    }
    if (in.offset != bytes.size()) { return {}; }
    error.clear();
    return state;
}
}
