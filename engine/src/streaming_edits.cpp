#include <lykoware/streaming_edits.hpp>

#include <stdexcept>
#include <utility>

namespace lykoware::streaming {
RegionEdits::RegionEdits(std::size_t byte_limit, std::size_t entry_limit)
    : byte_limit_(byte_limit), entry_limit_(entry_limit)
{
    if (byte_limit == 0 || entry_limit == 0) {
        throw std::invalid_argument("edit journal limits must be positive");
    }
}
bool RegionEdits::record(Region region, std::string asset,
                         std::vector<std::byte> bytes, std::string& error)
{
    error.clear();
    if (asset.empty()) { error = "edited asset ID is empty"; return false; }
    const Key key{region.x, region.z, std::move(asset)};
    const auto prior = edits_.find(key);
    const auto retained = bytes_ - (prior == edits_.end() ? 0 : prior->second.size());
    if (bytes.size() > byte_limit_ - retained
        || (prior == edits_.end() && edits_.size() == entry_limit_)) {
        error = "edit journal limit exceeded";
        return false;
    }
    const auto total = retained + bytes.size();
    if (prior == edits_.end()) { edits_.emplace(key, std::move(bytes)); }
    else { prior->second.swap(bytes); }
    bytes_ = total;
    return true;
}
bool RegionEdits::apply(Region region, RegionData& authored, std::string& error) const
{
    error.clear();
    const auto first = edits_.lower_bound(Key{region.x, region.z, {}});
    auto last = first;
    while (last != edits_.end() && std::get<0>(last->first) == region.x
           && std::get<1>(last->first) == region.z) { ++last; }
    if (first == last) { return true; }
    auto candidate = authored;
    for (auto edit = first; edit != last; ++edit) {
        ResidentAsset* target = nullptr;
        for (auto& asset : candidate.assets) {
            if (asset.id == std::get<2>(edit->first)) {
                if (target) { error = "edited asset ID is ambiguous"; return false; }
                target = &asset;
            }
        }
        if (!target) { error = "edited asset is absent from authored region"; return false; }
        target->bytes = edit->second;
    }
    authored.assets.swap(candidate.assets);
    return true;
}
void RegionEdits::clear() { edits_.clear(); bytes_ = 0; }
std::size_t RegionEdits::bytes() const { return bytes_; }
std::size_t RegionEdits::size() const { return edits_.size(); }
std::vector<RegionEdit> RegionEdits::snapshot() const
{
    std::vector<RegionEdit> result;
    result.reserve(edits_.size());
    for (const auto& [key, bytes] : edits_) {
        result.push_back({{std::get<0>(key), std::get<1>(key)}, std::get<2>(key), bytes});
    }
    return result;
}
}
