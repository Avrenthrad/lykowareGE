#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace lykoware::assets {
struct Asset {
    std::vector<std::uint8_t> bytes;
    std::vector<std::string> dependencies;
};
// ADR 0082. Owns all bytes/IDs/edges. Lookups borrow until mutation/destruction.
// One writer per object; concurrent const reads allowed. No source-tree access.
using Package = std::map<std::string, Asset, std::less<>>;
// False + diagnostic on bad data/IO/explicit byte limit, output unchanged.
// Allocation errors propagate. No external dependencies or runtime side effects.
[[nodiscard]] bool decode(std::string_view text, std::size_t max_bytes, Package& output, std::string& error);
[[nodiscard]] bool load(const std::filesystem::path& path, std::size_t max_bytes, Package& output, std::string& error);
}
