#pragma once
#include "rig_roles.hpp"
#include <lykoware/ik.hpp>
#include <lykoware/physics.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace foot_placement {
namespace math = lykoware::math;
namespace animation = lykoware::animation;
using V = math::DVec3;
inline bool finite(V p) { return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z); }
inline double norm(V p) { return std::hypot(p.x,p.y,p.z); }
inline V scale(V p,double s) { return {p.x*s,p.y*s,p.z*s}; }
inline V cross(V a,V b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
inline V rotate(math::Quat q,V p)
{
    const V u{q.x,q.y,q.z};
    return p+scale(cross(u,cross(u,p)+scale(p,q.w)),2.0);
}
struct Placement {
    V feet{};
    math::Quat rotation{};
    double uniform_scale=1.0;
    V offset{};
};
inline bool valid(const Placement& p)
{
    const auto q=p.rotation;
    const double n=static_cast<double>(q.x)*q.x+static_cast<double>(q.y)*q.y+static_cast<double>(q.z)*q.z+static_cast<double>(q.w)*q.w;
    return finite(p.feet)&&finite(p.offset)&&std::isfinite(p.uniform_scale)&&p.uniform_scale>0.0
        && std::isfinite(1.0/p.uniform_scale)&&std::isfinite(n)&&std::abs(n-1.0)<1e-5;
}
inline V to_world(const Placement& p,V model) { return p.feet+rotate(p.rotation,scale(model,p.uniform_scale)+p.offset); }
inline V to_model(const Placement& p,V world) { return scale(rotate(math::conjugate(p.rotation),world-p.feet)-p.offset,1.0/p.uniform_scale); }
inline V position(const math::Mat4& m) { return {m.m[3][0],m.m[3][1],m.m[3][2]}; }
struct Stats {
    std::uint64_t frames=0, contacts=0, misses=0, clamped=0, resets=0;
};
class Controller {
public:
    bool configure(const animation::Skeleton& skeleton,const rig_roles::Indices& roles,std::string& error,double clearance=0.03)
    {
        if(!std::isfinite(clearance)||clearance<0.0||clearance>0.3){error="Foot clearance must be 0..0.3 metres";return false;}
        if (!animation::valid_skeleton(skeleton,error)) { return false; }
        for (auto index:roles) { if(index>=skeleton.joints.size()){error="Foot IK role outside skeleton";return false;} }
        for(std::size_t i=0;i<roles.size();++i){for(std::size_t j=0;j<i;++j){if(roles[i]==roles[j]){error="Foot IK roles must be distinct";return false;}}}
        auto ancestor=[&](std::size_t a,std::size_t b){
            auto p=skeleton.joints[b].parent;
            while(p){if(*p==a){return true;}p=skeleton.joints[*p].parent;}
            return false;
        };
        if(ancestor(roles[1],roles[4])||ancestor(roles[4],roles[1])){error="Foot IK legs must be independent branches";return false;}
        auto rest=animation::rest_pose(skeleton);
        std::vector<math::Mat4> fk;
        animation::global_transforms(skeleton,rest,fk);
        std::array<std::optional<animation::PreparedTwoBone>,2> prepared;
        for (std::size_t leg : {std::size_t{1},std::size_t{4}}) {
            auto parent=skeleton.joints[roles[leg]].parent;
            bool pelvis=false;
            while(parent){if(*parent==roles[0]){pelvis=true;break;}parent=skeleton.joints[*parent].parent;}
            if(!pelvis){error="Foot IK hip must descend from pelvis";return false;}
            const auto side=leg==1?std::size_t{0}:std::size_t{1};
            prepared[side]=animation::PreparedTwoBone::prepare(skeleton,roles[leg],roles[leg+1],roles[leg+2],error);
            if(!prepared[side]){return false;}
            animation::Pose checked;
            animation::TwoBoneSolution solved;
            if(!animation::pose_two_bone(skeleton,rest,roles[leg],roles[leg+1],roles[leg+2],position(fk[roles[leg+2]]),
                position(fk[roles[leg+1]])+V{0,0,-0.5},checked,solved,error)){return false;}
        }
        animation::Skeleton copy=skeleton;
        animation::Pose candidate(rest.size());
        skeleton_=std::move(copy);prepared_=std::move(prepared);candidate_=std::move(candidate);fk_=std::move(fk);
        roles_=roles;clearance_=clearance;ready_=true;reset();stats_={};return true;
    }
    void reset() { correction_={};previous_=false;++stats_.resets; }
    const Stats& stats() const { return stats_; }
    // Transactional pose/contact state; caller can preserve base animation on false.
    bool update(const animation::Pose& base,const Placement& actor,const lykoware::physics::World& world,
                float seconds,bool active,animation::Pose& out,std::string& error)
    {
        if(!ready_||!valid(actor)){error="Invalid foot IK configuration or placement";return false;}
        if(!prepared_[0]->valid_pose(base,error)){return false;}
        candidate_=base;
        auto next=correction_;
        Stats stats=stats_;++stats.frames;
        const bool jump=previous_&&norm(actor.feet-previous_feet_)>1.0;
        if(!active||!std::isfinite(seconds)||seconds<0.F||seconds>0.25F||jump){
            next={};++stats.resets;out=candidate_;correction_=next;previous_feet_=actor.feet;previous_=true;stats_=stats;error.clear();return true;
        }
        animation::global_transforms(skeleton_,base,fk_);
        for(std::size_t side=0;side<2;++side){
            const std::size_t leg=side==0?1:4;
            const V foot=to_world(actor,position(fk_[roles_[leg+2]]));
            constexpr double ray_limit=static_cast<double>(std::numeric_limits<float>::max())/4.0;
            if(!finite(foot)||std::abs(foot.x)>ray_limit||std::abs(foot.y)>ray_limit||std::abs(foot.z)>ray_limit){
                error="World foot exceeds finite physics ray range";return false;
            }
            const auto hit=world.cast_ray(foot+V{0,0.35,0},{0,-1,0});
            if(!hit||!finite(hit->point)||std::abs(hit->point.y+clearance_-foot.y)>0.3){next[side]=0.0;++stats.misses;continue;}
            const double desired=hit->point.y+clearance_-foot.y;
            const double alpha=1.0-std::exp(-12.0*seconds);
            next[side]+=alpha*(desired-next[side]);
            const V target=to_model(actor,foot+V{0,next[side],0});
            const V pole=position(fk_[roles_[leg+1]])+V{0,0,-0.5};
            animation::TwoBoneSolution solved;
            if(!prepared_[side]->apply(candidate_,target,pole,scratch_,candidate_,solved,error)){return false;}
            ++stats.contacts;
            if(solved.target_clamped){++stats.clamped;}
        }
        out=candidate_;correction_=next;previous_feet_=actor.feet;previous_=true;stats_=stats;error.clear();return true;
    }
private:
    double clearance_=0.03;
    animation::Skeleton skeleton_;
    std::array<std::optional<animation::PreparedTwoBone>,2> prepared_;
    animation::TwoBoneScratch scratch_;
    animation::Pose candidate_;
    std::vector<math::Mat4> fk_;
    rig_roles::Indices roles_{};
    std::array<double,2> correction_{};
    V previous_feet_{};
    bool ready_=false,previous_=false;
    Stats stats_{};
};
}
