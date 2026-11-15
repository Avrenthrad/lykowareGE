#include <lykoware/streaming_chunks.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace lykoware::streaming {
namespace {
struct PendingBodies {
    ChunkCollision& collision;
    std::vector<physics::BodyId> bodies;
    ~PendingBodies()
    {
        for (const auto body : bodies) { collision.destroy(body); }
    }
};
}
ChunkStreamer::ChunkStreamer(ChunkCollision collision, std::size_t byte_limit, std::size_t region_limit)
    : collision_(std::move(collision)), residency_(byte_limit, region_limit)
{
    if (!collision_.create || !collision_.destroy) { throw std::invalid_argument("chunk collision callbacks required"); }
}
ChunkStreamer::~ChunkStreamer()
{
    for (const auto& [key, record] : records_) {
        (void)key;
        for (const auto body : record.bodies) { collision_.destroy(body); }
    }
}
bool ChunkStreamer::stream(Position point, ChunkDefinition chunk, std::string& error)
{
    error.clear();
    const auto region = region_at(point);
    if (!region || *region != chunk.region || !std::isfinite(chunk.pose.x)
        || !std::isfinite(chunk.pose.y) || !std::isfinite(chunk.pose.z)) {
        error = "invalid point, chunk region or pose";
        return false;
    }
    if (residency_.find(*region)) { return true; }
    const auto has = [&](const std::string& id) {
        return !id.empty() && std::any_of(chunk.data.assets.begin(), chunk.data.assets.end(),
                                         [&](const ResidentAsset& asset) { return asset.id == id; });
    };
    if (!has(chunk.coarse_asset) || !has(chunk.fine_asset)) {
        error = "chunk coarse/fine payload missing";
        return false;
    }
    for (const auto& desc : chunk.collision) {
        if (desc.motion != physics::MotionType::Static) {
            error = "streamed chunk collision must be static";
            return false;
        }
    }
    auto candidate = residency_;
    if (!candidate.install(*region, std::move(chunk.data), error)) { return false; }
    auto records = records_;
    for (auto it = records.begin(); it != records.end();) {
        if (!candidate.contains({it->first.first, it->first.second})) { it = records.erase(it); }
        else { ++it; }
    }
    PendingBodies pending{collision_, {}};
    pending.bodies.reserve(chunk.collision.size());
    for (const auto& desc : chunk.collision) {
        const auto body = collision_.create(desc, error);
        if (!body) { return false; }
        pending.bodies.push_back(*body);
    }
    records.emplace(Key{region->x, region->z}, Record{chunk.pose, std::move(chunk.coarse_asset),
                                                   std::move(chunk.fine_asset), pending.bodies});
    // All potentially throwing allocations precede publication.
    residency_ = std::move(candidate);
    records_.swap(records);
    pending.bodies.clear();
    for (const auto& [key, record] : records) {
        if (!records_.contains(key)) {
            for (const auto body : record.bodies) { collision_.destroy(body); }
        }
    }
    return true;
}
std::optional<ChunkView> ChunkStreamer::query(Region region)
{
    const auto found = records_.find({region.x, region.z});
    if (found == records_.end()) { return std::nullopt; }
    const auto* data = residency_.find(region);
    const auto payload = [&](const std::string& id) -> std::span<const std::byte> {
        for (const auto& asset : data->assets) {
            if (asset.id == id) { return asset.bytes; }
        }
        return {};
    };
    return ChunkView{region_id(region), found->second.pose, payload(found->second.coarse), payload(found->second.fine)};
}
bool ChunkStreamer::pin(Region region, bool pinned) { return residency_.pin(region, pinned); }
std::size_t ChunkStreamer::bytes() const { return residency_.bytes(); }
std::size_t ChunkStreamer::size() const { return residency_.size(); }
}
