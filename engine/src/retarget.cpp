#include <algorithm>
#include <cmath>
#include <lykoware/retarget.hpp>
namespace lykoware::animation {
std::unique_ptr<Retarget> Retarget::create(const Skeleton& source, const Skeleton& target, float scale,
                                           std::string& error)
{
    if (!std::isfinite(scale) || scale <= 0 || scale > 1000) {
        error = "Root translation scale must be in (0,1000]";
        return nullptr;
    }
    const auto match = character::compare_rigs(source, target);
    if (!match.compatible) {
        error = match.issues.empty() ? "Incompatible retarget rigs" : match.issues.front();
        return nullptr;
    }
    auto plan = std::unique_ptr<Retarget>(new Retarget);
    plan->source_ = source;
    plan->target_ = target;
    plan->mapping_ = match.joints;
    plan->basis_ = match.root_basis;
    plan->root_scale_ = scale;
    error.clear();
    return plan;
}
bool Retarget::apply(std::span<const JointPose> source, std::span<JointPose> target, std::string& error) const
{
    if (source.size() != source_.joints.size() || target.size() != target_.joints.size()) {
        error = "Retarget pose sizes do not match prepared rigs";
        return false;
    }
    std::array<JointPose, max_joints> candidate{};
    std::array<math::Mat4, max_joints> globals{};
    for (const auto& pair : mapping_) {
        const auto& s = source[pair.source];
        const auto& rest = source_.joints[pair.source];
        const auto& t = target_.joints[pair.target];
        const auto q = s.rotation;
        if (!std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.z) || !std::isfinite(q.w) ||
            std::abs(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w - 1.F) > 1e-3F ||
            s.scale != math::Vec3{1, 1, 1} || (rest.parent && s.translation != rest.rest.translation)) {
            error = "Retarget requires unit rotations/scales and unchanged non-root translations";
            return false;
        }
        auto& out = candidate[pair.target];
        out = t.rest;
        out.rotation = math::normalize(t.rest.rotation * math::conjugate(rest.rest.rotation) * s.rotation);
        if (!rest.parent) {
            out.translation = t.rest.translation +
                              math::rotate(basis_, s.translation - rest.rest.translation) * root_scale_;
        }
        for (float v : {out.translation.x, out.translation.y, out.translation.z}) {
            if (!std::isfinite(v) || std::abs(v) > 1e6F) {
                error = "Retarget translation exceeds finite model range";
                return false;
            }
        }
    }
    for (std::size_t i = 0; i < target.size(); ++i) {
        const auto& p = candidate[i];
        auto m = math::trs(p.translation, p.rotation, p.scale);
        if (target_.joints[i].parent) {
            m = globals[*target_.joints[i].parent] * m;
        }
        for (const auto& column : m.m) {
            for (float v : column) {
                if (!std::isfinite(v)) {
                    error = "Retarget hierarchy overflow";
                    return false;
                }
            }
        }
        globals[i] = m;
    }
    std::copy_n(candidate.begin(), target.size(), target.begin());
    error.clear();
    return true;
}
} // namespace lykoware::animation
