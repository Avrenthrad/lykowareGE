#include <lykoware/streaming_systems.hpp>

#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace lykoware::streaming {
namespace {
navigation::Grid make_grid(navigation::Config config)
{
    std::string error;
    auto grid = navigation::Grid::create(config, error);
    if (!grid) { throw std::invalid_argument(error); }
    return std::move(*grid);
}
struct PendingEntities {
    ecs::World& world;
    std::vector<ecs::Entity> entities;
    ~PendingEntities()
    {
        for (const auto entity : entities) { (void)world.destroy(entity); }
    }
};
}
RegionSystems::RegionSystems(navigation::Config navigation, ecs::World& world)
    : world_(world), navigation_(make_grid(navigation)) {}
RegionSystems::~RegionSystems()
{
    while (!active_.empty()) {
        const auto key = active_.begin()->first;
        deactivate({key.first, key.second});
    }
}
bool RegionSystems::stage(Region region, RegionContent content, std::string& error)
{
    error.clear();
    if (world_.locked() || active_.contains({region.x, region.z})) {
        error = "cannot stage with locked ECS or active region";
        return false;
    }
    for (const auto position : content.entities) {
        if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) {
            error = "invalid authored entity position";
            return false;
        }
    }
    staged_.insert_or_assign(Key{region.x, region.z}, std::move(content));
    return true;
}
bool RegionSystems::activate(Region region, std::string& error)
{
    error.clear();
    const Key key{region.x, region.z};
    const auto source = staged_.find(key);
    if (world_.locked() || source == staged_.end() || active_.contains(key)) {
        error = "locked ECS, missing staged content or already active region";
        return false;
    }
    const auto& content = source->second;
    if (content.solids.size() > std::numeric_limits<std::uint64_t>::max() - next_solid_) {
        error = "navigation solid IDs exhausted";
        return false;
    }
    auto grid = navigation_;
    Active incoming;
    incoming.bank = content.bank;
    incoming.solids.reserve(content.solids.size());
    auto next = next_solid_;
    for (const auto solid : content.solids) {
        if (!grid.upsert(next, solid)) {
            error = "invalid streamed navigation solid";
            return false;
        }
        incoming.solids.push_back(next++);
    }
    PendingEntities pending{world_, {}};
    pending.entities.reserve(content.entities.size());
    for (const auto position : content.entities) {
        const auto entity = world_.create();
        if (!world_.alive(entity)) {
            error = "streamed entity creation refused";
            return false;
        }
        pending.entities.push_back(entity);
        if (!world_.add<StreamedEntity>(entity, StreamedEntity{region, position})) {
            error = "streamed entity component refused";
            return false;
        }
    }
    incoming.entities = pending.entities;
    active_.emplace(key, std::move(incoming));
    static_assert(std::is_nothrow_move_assignable_v<lykoware::navigation::Grid>);
    navigation_ = std::move(grid);
    next_solid_ = next;
    staged_.erase(source);
    pending.entities.clear();
    return true;
}
void RegionSystems::deactivate(Region region)
{
    const auto found = active_.find({region.x, region.z});
    if (found == active_.end()) { return; }
    for (const auto solid : found->second.solids) { (void)navigation_.erase(solid); }
    for (const auto entity : found->second.entities) { (void)world_.destroy(entity); }
    active_.erase(found);
}
ChunkLifecycle RegionSystems::lifecycle()
{
    return {
        [this](Region region, std::string& error) { return activate(region, error); },
        [this](Region region) { deactivate(region); }
    };
}
const navigation::Grid& RegionSystems::navigation() const { return navigation_; }
std::shared_ptr<const audio::Sound> RegionSystems::bank(Region region) const
{
    const auto found = active_.find({region.x, region.z});
    return found == active_.end() ? nullptr : found->second.bank;
}
}
