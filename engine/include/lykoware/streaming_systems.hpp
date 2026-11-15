#pragma once

#include <lykoware/audio.hpp>
#include <lykoware/ecs.hpp>
#include <lykoware/navigation.hpp>
#include <lykoware/streaming_chunks.hpp>

namespace lykoware::streaming {
struct RegionContent {
    std::vector<navigation::Solid> solids;
    std::shared_ptr<const audio::Sound> bank;
    std::vector<Position> entities;
};
struct StreamedEntity {
    Region region;
    Position authored;
};
// GPT-owned subsystem lifetime adapter (ADR 0094). Owns staged/active content and
// its nav grid; borrows ECS. ECS and this adapter must outlive a ChunkStreamer
// using lifecycle(). One controlling thread, outside ECS iteration/hooks.
// stage/activate reject invalid/locked input without partial activation; allocation
// errors propagate with new entities rolled back. deactivate/destruction require
// an unlocked ECS and do not throw. Callers must not destroy managed entities or
// install component-removal hooks. Returned bank handles own a shared reference.
class RegionSystems {
public:
    RegionSystems(navigation::Config navigation, ecs::World& world);
    ~RegionSystems();
    RegionSystems(const RegionSystems&) = delete;
    RegionSystems& operator=(const RegionSystems&) = delete;
    [[nodiscard]] bool stage(Region region, RegionContent content, std::string& error);
    [[nodiscard]] bool activate(Region region, std::string& error);
    void deactivate(Region region);
    [[nodiscard]] ChunkLifecycle lifecycle(); // Borrows this until streamer destruction.
    [[nodiscard]] const navigation::Grid& navigation() const;
    [[nodiscard]] std::shared_ptr<const audio::Sound> bank(Region region) const;
private:
    using Key = std::pair<std::int32_t, std::int32_t>;
    struct Active {
        std::vector<std::uint64_t> solids;
        std::vector<ecs::Entity> entities;
        std::shared_ptr<const audio::Sound> bank;
    };
    ecs::World& world_;
    navigation::Grid navigation_;
    std::map<Key, RegionContent> staged_;
    std::map<Key, Active> active_;
    std::uint64_t next_solid_ = 1;
};
}
