#pragma once

#include <lykoware/physics.hpp>
#include <lykoware/streaming_residency.hpp>

#include <functional>
#include <map>
#include <span>
#include <utility>

namespace lykoware::streaming {
struct ChunkDefinition {
    Region region;
    Position pose;
    RegionData data;
    std::string coarse_asset, fine_asset;
    std::vector<physics::BodyDesc> collision; // Static, absolute-world coordinates.
};
struct ChunkView {
    std::string id;
    Position pose;
    std::span<const std::byte> coarse, fine;
};
struct ChunkCollision {
    std::function<std::optional<physics::BodyId>(const physics::BodyDesc&, std::string&)> create;
    std::function<void(physics::BodyId)> destroy; // Must not throw.
};
struct ChunkLifecycle {
    // Paired or both empty. activate must roll back its own changes on failure.
    std::function<bool(Region, std::string&)> activate;
    std::function<void(Region)> deactivate; // Must not throw; outside ECS iteration.
};
// Borrows world until streamer destruction; uses existing create/destroy only.
[[nodiscard]] ChunkCollision physics_chunk_collision(physics::World& world);

// GPT-owned chunk lifetime (ADR 0093). Owns resident payloads/created bodies;
// callback captures must outlive this object. Single controlling thread. A view
// borrows bytes for one frame, until next mutation/destruction. Never retain it.
// Invalid input/admission/physics failure returns false with error, preserving
// prior residency; exceptions propagate with new bodies rolled back. Callbacks
// must honour ChunkCollision's no-throw destroy contract. Explicit cache limits.
class ChunkStreamer {
public:
    ChunkStreamer(ChunkCollision collision, std::size_t byte_limit, std::size_t region_limit,
                  ChunkLifecycle lifecycle = {});
    ~ChunkStreamer();
    ChunkStreamer(const ChunkStreamer&) = delete;
    ChunkStreamer& operator=(const ChunkStreamer&) = delete;
    [[nodiscard]] bool stream(Position point, ChunkDefinition chunk, std::string& error);
    [[nodiscard]] std::optional<ChunkView> query(Region region);
    [[nodiscard]] bool pin(Region region, bool pinned);
    [[nodiscard]] std::size_t bytes() const;
    [[nodiscard]] std::size_t size() const;
private:
    struct Record {
        Position pose;
        std::string coarse, fine;
        std::vector<physics::BodyId> bodies;
    };
    using Key = std::pair<std::int32_t, std::int32_t>;
    ChunkCollision collision_;
    ChunkLifecycle lifecycle_;
    Residency residency_;
    std::map<Key, Record> records_;
};
}
