#pragma once

#include <lykoware/streaming.hpp>

#include <list>
#include <memory>
#include <string>
#include <vector>

namespace lykoware::streaming {
struct ResidentAsset {
    std::string id;
    std::vector<std::byte> bytes;
    std::vector<std::string> dependencies;
};
struct RegionData {
    std::vector<ResidentAsset> assets;
};
// GPT-owned region cache (ADR 0092). Owns immutable bundles; find borrows until
// successful mutation/destruction. One controlling thread; no concurrent calls.
// Explicit positive payload-byte and region limits (not total process memory).
// Invalid bundles/pin pressure return false with error, preserving state/order;
// allocation errors propagate. Pins protect every asset of a region. No ECS or
// physics mutation. find touches LRU; contains/size/bytes do not.
class Residency {
public:
    Residency(std::size_t byte_limit, std::size_t region_limit);
    [[nodiscard]] bool install(Region region, RegionData data, std::string& error);
    [[nodiscard]] const RegionData* find(Region region);
    [[nodiscard]] bool contains(Region region) const;
    [[nodiscard]] bool pin(Region region, bool pinned);
    [[nodiscard]] bool erase(Region region);
    [[nodiscard]] std::size_t bytes() const;
    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] std::vector<Region> regions() const;
private:
    struct Entry {
        Region region;
        std::shared_ptr<const RegionData> data;
        std::size_t bytes = 0;
        bool pinned = false;
    };
    std::size_t byte_limit_, region_limit_, bytes_ = 0;
    std::list<Entry> entries_; // Most recent first.
};
}
