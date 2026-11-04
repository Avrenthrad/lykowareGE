#pragma once
#include <cstdint>
namespace lykoware::detail {
// Process-local, never serialized or hashed. Throws before identity can wrap.
std::uint64_t next_world_identity();
}
