#pragma once
#include <lykoware/math.hpp>
#include <string>

namespace lykoware::animation {
struct TwoBoneRequest {
    math::DVec3 root{};
    math::DVec3 target{1.0,0.0,0.0};
    math::DVec3 bend_hint{0.0,1.0,0.0};
    math::DVec3 fallback_forward{1.0,0.0,0.0};
    double upper_length = 1.0;
    double lower_length = 1.0;
};
struct TwoBoneSolution {
    math::DVec3 root{}, knee{}, end{};
    bool target_clamped = false;
    bool used_fallback_direction = false;
    bool used_fallback_bend = false;
};
// Drawing-space position solver, ADR 0057. Inputs share a coordinate space. Lengths are
// 1e-6..1e6 m. No stretch: unreachable targets clamp to [abs(a-b), a+b]. A root-coincident
// target uses fallback_forward; collinear hints use a deterministic perpendicular axis.
// Refuses nonfinite/degenerate directions and unrepresentable output, with reason.
// Transactional output; pure CPU data, separate outputs may be solved independently.
// Does not compute joint rotations or modify poses/physics. Trig/sqrt rounding is visual,
// not a cross-platform bit-identical simulation guarantee.
[[nodiscard]] bool solve_two_bone(const TwoBoneRequest& request, TwoBoneSolution& out, std::string& error);
}
