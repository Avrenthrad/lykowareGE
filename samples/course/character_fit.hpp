#pragma once
#include <lykoware/mesh.hpp>
#include <cmath>
#include <string>

namespace character_fit {
struct Placement {
    float scale = 1.F;
    lykoware::math::Vec3 offset{};
};
// Visual uniform placement only. No skeleton, clip or physics mutation. Transactional output.
inline bool fit(const lykoware::Bounds& bounds, float height, Placement& out, std::string& error)
{
    error.clear();
    const float values[] = {bounds.min.x,bounds.min.y,bounds.min.z,bounds.max.x,bounds.max.y,bounds.max.z,height};
    for (float v : values) {
        if (!std::isfinite(v)) { error = "Character fitting requires finite bounds and height"; return false; }
    }
    if (height < 0.1F || height > 10.F || bounds.max.x < bounds.min.x || bounds.max.z < bounds.min.z
        || !(bounds.max.y > bounds.min.y)) {
        error = "Character fitting requires ordered, nonzero-height bounds and 0.1..10 metres"; return false;
    }
    const double scale = height / (static_cast<double>(bounds.max.y) - bounds.min.y);
    Placement result;
    result.scale = static_cast<float>(scale);
    result.offset = {static_cast<float>(-(static_cast<double>(bounds.min.x) + bounds.max.x) * 0.5 * scale),
                     static_cast<float>(-static_cast<double>(bounds.min.y) * scale),
                     static_cast<float>(-(static_cast<double>(bounds.min.z) + bounds.max.z) * 0.5 * scale)};
    if (!std::isfinite(result.scale) || result.scale <= 0.F || !std::isfinite(result.offset.x)
        || !std::isfinite(result.offset.y) || !std::isfinite(result.offset.z)) {
        error = "Character fitting exceeds finite float range"; return false;
    }
    out = result;
    return true;
}
}
