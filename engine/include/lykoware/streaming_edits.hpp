#pragma once

#include <lykoware/streaming_residency.hpp>

#include <map>
#include <tuple>

namespace lykoware::streaming {
// GPT-owned edit journal (ADR 0095). Owns replacement bytes until clear/destruction,
// independently of chunk residency. Single controlling thread; no borrowed output.
// Explicit limits count payload bytes and entries, not total process memory.
// Invalid IDs, limits or stale authored IDs return false with error and no changes.
// Allocation errors propagate without changing the journal or apply's output.
class RegionEdits {
public:
    RegionEdits(std::size_t byte_limit, std::size_t entry_limit);
    [[nodiscard]] bool record(Region region, std::string asset,
                              std::vector<std::byte> bytes, std::string& error);
    [[nodiscard]] bool apply(Region region, RegionData& authored, std::string& error) const;
    void clear();
    [[nodiscard]] std::size_t bytes() const;
    [[nodiscard]] std::size_t size() const;
private:
    using Key = std::tuple<std::int32_t, std::int32_t, std::string>;
    std::map<Key, std::vector<std::byte>> edits_;
    std::size_t byte_limit_, entry_limit_, bytes_ = 0;
};
}
