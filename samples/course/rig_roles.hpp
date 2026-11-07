#pragma once
#include <lykoware/animation.hpp>
#include <array>
#include <cstddef>
#include <optional>
#include <string>

namespace rig_roles {
inline constexpr std::array<const char*,7> names{{"pelvis","left_hip","left_knee","left_foot","right_hip","right_knee","right_foot"}};
using Names = std::array<std::string,7>;
using Indices = std::array<std::size_t,7>;
// Transactional lookup against validated skeleton order; no pose or simulation mutation.
inline bool resolve(const lykoware::animation::Skeleton& skeleton, const Names& requested, Indices& out, std::string& error)
{
    if (!lykoware::animation::valid_skeleton(skeleton,error)) { return false; }
    Indices found{};
    for (std::size_t role=0;role<names.size();++role) {
        std::optional<std::size_t> match;
        for (std::size_t joint=0;joint<skeleton.joints.size();++joint) {
            if (!requested[role].empty() && skeleton.joints[joint].name==requested[role]) {
                if (match) { error="Ambiguous bone name: "+requested[role]; return false; }
                match=joint;
            }
        }
        if (!match) { error="Missing bone role: "+std::string(names[role]); return false; }
        found[role]=*match;
        for (std::size_t previous=0;previous<role;++previous) {
            if (found[previous]==found[role]) { error="Bone roles must name distinct joints";return false; }
        }
    }
    auto ancestor=[&](std::size_t parent,std::size_t child) {
        auto current=skeleton.joints[child].parent;
        while (current) {
            if (*current==parent) { return true; }
            current=skeleton.joints[*current].parent;
        }
        return false;
    };
    for (std::size_t first : {std::size_t{1},std::size_t{4}}) {
        if (!ancestor(found[0],found[first]) || !ancestor(found[first],found[first+1]) || !ancestor(found[first+1],found[first+2])) {
            error="Bone roles require pelvis/hip/knee/foot ancestry";return false;
        }
    }
    out=found;error.clear();return true;
}
}
