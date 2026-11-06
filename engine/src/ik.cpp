#include <lykoware/ik.hpp>
#include <algorithm>
#include <cmath>

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
