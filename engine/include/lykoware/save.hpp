#pragma once

#include <lykoware/streaming_edits.hpp>

#include <cstdint>
#include <optional>
#include <span>

namespace lykoware::save {
inline constexpr std::uint32_t format_version = 2;
inline constexpr std::uint32_t world_version = 1;
struct Limits {
    std::size_t file_bytes, payload_bytes, entries, id_bytes;
};
struct State {
    std::uint32_t world_schema = world_version;
    std::vector<streaming::RegionEdit> edits;
};
// GPT-owned save codec (ADR 0102). Single controlling thread. Inputs are borrowed
// only during calls; results own all bytes. Positive explicit limits are required.
// Malformed/unsupported/over-limit data returns nullopt with error, without live
// world mutation. Allocation errors propagate. No filesystem writes in this API.
[[nodiscard]] std::optional<std::vector<std::byte>> encode(
    const streaming::RegionEdits& edits, Limits limits, std::string& error);
[[nodiscard]] std::optional<State> decode(
    std::span<const std::byte> bytes, Limits limits, std::string& error);
}
