#include <lykoware/streaming_residency.hpp>

#include <algorithm>
#include <map>
#include <stdexcept>
#include <utility>

namespace lykoware::streaming {
Residency::Residency(std::size_t byte_limit, std::size_t region_limit)
    : byte_limit_(byte_limit), region_limit_(region_limit)
{
    if (byte_limit == 0 || region_limit == 0) {
        throw std::invalid_argument("residency limits must be positive");
    }
}
bool Residency::install(Region region, RegionData data, std::string& error)
{
    error.clear();
    const auto fail = [&](const char* message) { error = message; return false; };
    std::size_t count = 0;
    std::map<std::string, std::size_t> ids;
    for (std::size_t i = 0; i < data.assets.size(); ++i) {
        const auto& asset = data.assets[i];
        if (asset.id.empty() || !ids.emplace(asset.id, i).second) {
            return fail("empty or duplicate resident asset ID");
        }
        if (asset.bytes.size() > byte_limit_ - count) {
            return fail("region exceeds residency byte limit");
        }
        count += asset.bytes.size();
    }
    // Kahn's algorithm avoids recursion on authored dependency chains.
    std::vector<std::size_t> incoming(data.assets.size(), 0);
    std::vector<std::vector<std::size_t>> dependents(data.assets.size());
    for (std::size_t i = 0; i < data.assets.size(); ++i) {
        for (const auto& dependency : data.assets[i].dependencies) {
            const auto found = ids.find(dependency);
            if (found == ids.end()) {
                return fail("missing resident asset dependency");
            }
            ++incoming[i];
            dependents[found->second].push_back(i);
        }
    }
    std::vector<std::size_t> ready;
    for (std::size_t i = 0; i < incoming.size(); ++i) {
        if (incoming[i] == 0) { ready.push_back(i); }
    }
    for (std::size_t i = 0; i < ready.size(); ++i) {
        for (const auto dependent : dependents[ready[i]]) {
            if (--incoming[dependent] == 0) { ready.push_back(dependent); }
        }
    }
    if (ready.size() != data.assets.size()) {
        return fail("cyclic resident asset dependencies");
    }
    auto candidate = entries_;
    std::size_t retained = bytes_;
    const auto previous = std::find_if(candidate.begin(), candidate.end(), [&](const Entry& e) { return e.region == region; });
    if (previous != candidate.end()) {
        if (previous->pinned) { return fail("cannot replace pinned region"); }
        retained -= previous->bytes;
        candidate.erase(previous);
    }
    while (count > byte_limit_ - retained || candidate.size() >= region_limit_) {
        const auto victim = std::find_if(candidate.rbegin(), candidate.rend(), [](const Entry& e) { return !e.pinned; });
        if (victim == candidate.rend()) { return fail("pinned regions prevent admission"); }
        retained -= victim->bytes;
        candidate.erase(std::prev(victim.base()));
    }
    candidate.push_front({region, std::make_shared<const RegionData>(std::move(data)), count, false});
    entries_.swap(candidate);
    bytes_ = retained + count;
    return true;
}
const RegionData* Residency::find(Region region)
{
    const auto found = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.region == region; });
    if (found == entries_.end()) { return nullptr; }
    entries_.splice(entries_.begin(), entries_, found);
    return entries_.front().data.get();
}
bool Residency::contains(Region region) const
{
    return std::any_of(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.region == region; });
}
bool Residency::pin(Region region, bool pinned)
{
    const auto found = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.region == region; });
    if (found == entries_.end()) { return false; }
    found->pinned = pinned;
    return true;
}
bool Residency::erase(Region region)
{
    const auto found = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.region == region; });
    if (found == entries_.end() || found->pinned) { return false; }
    bytes_ -= found->bytes;
    entries_.erase(found);
    return true;
}
std::size_t Residency::bytes() const { return bytes_; }
std::size_t Residency::size() const { return entries_.size(); }
std::vector<Region> Residency::regions() const
{
    std::vector<Region> result;
    for (const auto& entry : entries_) { result.push_back(entry.region); }
    return result;
}
}
