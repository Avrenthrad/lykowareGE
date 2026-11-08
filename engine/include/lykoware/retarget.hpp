#pragma once
#include <lykoware/character_asset.hpp>
#include <memory>
namespace lykoware::animation {
// ADR 0065. Immutable owned plan; per-call fixed scratch. Concurrent applications are safe.
class Retarget {
public:
    [[nodiscard]] static std::unique_ptr<Retarget> create(const Skeleton& source,const Skeleton& target,float root_translation_scale,std::string& error);
    // Complete spans may alias/overlap. Refusal preserves output. No allocation on success.
    [[nodiscard]] bool apply(std::span<const JointPose> source,std::span<JointPose> target,std::string& error) const;
private:
    Retarget()=default;
    Skeleton source_,target_;
    std::vector<character::JointPair> mapping_;
    math::Quat basis_{};
    float root_scale_=1;
};
}
