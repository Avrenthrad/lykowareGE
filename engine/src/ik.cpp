#include <lykoware/ik.hpp>
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace lykoware::animation {
namespace {
using V = math::DVec3;
bool finite(V v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
double dot(V a, V b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
double length(V v)
{
    return std::hypot(v.x, v.y, v.z);
}
V multiply(V v, double s)
{
    return {v.x * s, v.y * s, v.z * s};
}
V divide(V v, double s)
{
    return {v.x / s, v.y / s, v.z / s};
}
bool refuse(std::string& error, const char* reason)
{
    error = reason;
    return false;
}
}
bool solve_two_bone(const TwoBoneRequest& r, TwoBoneSolution& out, std::string& error)
{
    error.clear();
    if (!finite(r.root) || !finite(r.target) || !finite(r.bend_hint) || !finite(r.fallback_forward)
        || !std::isfinite(r.upper_length) || !std::isfinite(r.lower_length)
        || r.upper_length<1e-6 || r.lower_length<1e-6 || r.upper_length>1e6 || r.lower_length>1e6) {
        return refuse(error,"IK requires finite coordinates and lengths in 1e-6..1e6 metres");
    }
    V direction=r.target-r.root;
    double distance=length(direction);
    if (!finite(direction) || !std::isfinite(distance)) { return refuse(error,"IK target offset is unrepresentable"); }
    TwoBoneSolution result;
    result.root=r.root;
    if (distance==0.0) {
        direction=r.fallback_forward;
        const double fallback_length=length(direction);
        if (!(fallback_length>0.0) || !std::isfinite(fallback_length)) { return refuse(error,"IK fallback direction is degenerate"); }
        direction=divide(direction,fallback_length);
        result.used_fallback_direction=true;
    } else { direction=divide(direction,distance); }
    const double reach=std::clamp(distance,std::abs(r.upper_length-r.lower_length),r.upper_length+r.lower_length);
    result.target_clamped=reach!=distance;
    V bend=r.bend_hint-r.root;
    if (!finite(bend)) { return refuse(error,"IK bend offset is unrepresentable"); }
    // Scale before projection to avoid overflow for very distant finite hints.
    const double hint_length=length(bend);
    if (!std::isfinite(hint_length)) { return refuse(error,"IK bend hint is unrepresentable"); }
    if (hint_length>0.0) { bend=divide(bend,hint_length); }
    bend=bend-multiply(direction,dot(bend,direction));
    double bend_length=length(bend);
    if (bend_length<1e-10) {
        V axis = std::abs(direction.x)<=std::abs(direction.y) && std::abs(direction.x)<=std::abs(direction.z)
            ? V{1.0,0.0,0.0} : (std::abs(direction.y)<=std::abs(direction.z) ? V{0.0,1.0,0.0} : V{0.0,0.0,1.0});
        bend=axis-multiply(direction,dot(axis,direction));
        bend_length=length(bend);
        result.used_fallback_bend=true;
    }
    bend=divide(bend,bend_length);
    double along=0.0;
    if (reach>0.0) {
        // Difference of squares factored to reduce cancellation near equal lengths.
        along=(reach+(r.upper_length-r.lower_length)*(r.upper_length+r.lower_length)/reach)*0.5;
        along=std::clamp(along,-r.upper_length,r.upper_length);
    }
    const double perpendicular=std::sqrt(std::max(0.0,(r.upper_length-along)*(r.upper_length+along)));
    result.end=r.root+multiply(direction,reach);
    result.knee=r.root+multiply(direction,along)+multiply(bend,perpendicular);
    if (!finite(result.end) || !finite(result.knee)
        || std::abs(length(result.knee-result.root)-r.upper_length)>r.upper_length*1e-6
        || std::abs(length(result.end-result.knee)-r.lower_length)>r.lower_length*1e-6) {
        return refuse(error,"IK output loses limb lengths at this coordinate magnitude");
    }
    out=result;return true;
}
}

namespace lykoware::animation {
namespace {
math::DVec3 position(const math::Mat4& m)
{
    return {m.m[3][0], m.m[3][1], m.m[3][2]};
}
math::Vec3 unit(math::DVec3 v)
{
    const double n = std::hypot(v.x, v.y, v.z);
    return {static_cast<float>(v.x/n), static_cast<float>(v.y/n), static_cast<float>(v.z/n)};
}
math::Quat between(math::Vec3 a, math::Vec3 b)
{
    // Re-normalise in double: a float dot product can round near-opposite directions to
    // exactly -1, losing a small but visible correction at the end of the limb.
    const double na = std::hypot(a.x,a.y,a.z);
    const double nb = std::hypot(b.x,b.y,b.z);
    const math::DVec3 x{a.x/na,a.y/na,a.z/na};
    const math::DVec3 y{b.x/nb,b.y/nb,b.z/nb};
    const double cosine = std::clamp(dot(x,y), -1.0, 1.0);
    if (cosine < -1.0 + 1e-12) {
        const math::Vec3 axis = std::abs(a.x) < std::abs(a.y)
            ? (std::abs(a.x)<std::abs(a.z) ? math::Vec3{1,0,0} : math::Vec3{0,0,1})
            : (std::abs(a.y)<std::abs(a.z) ? math::Vec3{0,1,0} : math::Vec3{0,0,1});
        const auto n = math::normalize(math::cross(a,axis));
        return {n.x,n.y,n.z,0.F};
    }
    const double factor = std::sqrt(2.0*(1.0+cosine));
    return math::normalize(math::Quat{static_cast<float>((x.y*y.z-x.z*y.y)/factor),
        static_cast<float>((x.z*y.x-x.x*y.z)/factor), static_cast<float>((x.x*y.y-x.y*y.x)/factor),
        static_cast<float>(factor*0.5)});
}
std::vector<math::Quat> orientations(const Skeleton& skeleton,const Pose& pose)
{
    std::vector<math::Quat> out(pose.size());
    for (std::size_t i=0;i<pose.size();++i) {
        out[i]=math::normalize(skeleton.joints[i].parent ? out[*skeleton.joints[i].parent]*pose[i].rotation : pose[i].rotation);
    }
    return out;
}
}
bool pose_two_bone(const Skeleton& skeleton, const Pose& pose,
    std::size_t root, std::size_t knee, std::size_t end, math::DVec3 target, math::DVec3 hint,
    Pose& out, TwoBoneSolution& solution, std::string& error)
{
    if (!valid_skeleton(skeleton,error)) { return false; }
    if (pose.size()!=skeleton.joints.size() || root>=pose.size() || knee>=pose.size() || end>=pose.size()
        || skeleton.joints[knee].parent!=root || skeleton.joints[end].parent!=knee) {
        return refuse(error,"Pose IK requires a direct root/knee/end chain and complete pose");
    }
    Skeleton posed=skeleton;
    for (std::size_t i=0;i<pose.size();++i) {
        posed.joints[i].rest=pose[i];
        const auto a=pose[i].scale;
        const auto b=skeleton.joints[i].rest.scale;
        if (a!=math::Vec3{1,1,1} || b!=math::Vec3{1,1,1}) {
            return refuse(error,"Pose IK currently requires unit joint scales");
        }
    }
    if (!valid_skeleton(posed,error)) { return false; }
    std::vector<math::Mat4> rest, current;
    global_transforms(skeleton,rest_pose(skeleton),rest);
    global_transforms(skeleton,pose,current);
    const double upper=length(position(rest[knee])-position(rest[root]));
    const double lower=length(position(rest[end])-position(rest[knee]));
    const double current_upper=length(position(current[knee])-position(current[root]));
    const double current_lower=length(position(current[end])-position(current[knee]));
    if (!(upper>=1e-6 && lower>=1e-6 && upper<=1e6 && lower<=1e6)
        || !std::isfinite(current_upper) || !std::isfinite(current_lower) || std::abs(current_upper-upper)>upper*1e-4
        || std::abs(current_lower-lower)>lower*1e-4) {
        return refuse(error,"Pose IK requires nonzero, unchanged rest segment lengths");
    }
    TwoBoneRequest request;
    request.root=position(current[root]); request.target=target; request.bend_hint=hint;
    request.upper_length=current_upper; request.lower_length=current_lower;
    request.fallback_forward=position(current[knee])-request.root;
    TwoBoneSolution solved;
    if (!solve_two_bone(request,solved,error)) { return false; }
    Pose candidate=pose;
    auto rotation=orientations(skeleton,candidate);
    auto parent=skeleton.joints[root].parent;
    const auto parent_rotation=parent ? rotation[*parent] : math::Quat{};
    const auto change=between(unit(position(current[knee])-request.root),unit(solved.knee-solved.root));
    candidate[root].rotation=math::normalize(math::conjugate(parent_rotation)*change*rotation[root]);
    global_transforms(skeleton,candidate,current);
    rotation=orientations(skeleton,candidate);
    const auto knee_position=position(current[knee]);
    const auto lower_change=between(unit(position(current[end])-knee_position),unit(solved.end-solved.knee));
    candidate[knee].rotation=math::normalize(math::conjugate(rotation[root])*lower_change*rotation[knee]);
    global_transforms(skeleton,candidate,current);
    const double tolerance=1e-5+(upper+lower)*1e-4;
    if (!(length(position(current[knee])-solved.knee)<=tolerance) || !(length(position(current[end])-solved.end)<=tolerance)) {
        return refuse(error,"Pose IK failed forward-kinematic verification");
    }
    out=std::move(candidate); solution=solved; error.clear(); return true;
}
}
