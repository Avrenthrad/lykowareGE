#include <lykoware/ik.hpp>
#include <cmath>
#include <iostream>
#include <limits>

int main()
{
    using namespace lykoware;
    int failures=0;
    auto expect=[&](bool ok,const char* why){if(!ok){std::cerr<<"FAIL: "<<why<<'\n';++failures;}};
    auto len=[](math::DVec3 v){return std::hypot(v.x,v.y,v.z);};
    auto near=[&](math::DVec3 a,math::DVec3 b){return len(a-b)<1e-8;};
    animation::TwoBoneRequest r;
    animation::TwoBoneSolution s;
    std::string error;
    expect(animation::solve_two_bone(r,s,error) && near(s.knee,{0.5,std::sqrt(0.75),0.0}) && near(s.end,r.target),"analytical equilateral bend");
    r.bend_hint={0,0,1};
    expect(animation::solve_two_bone(r,s,error) && near(s.knee,{0.5,0,std::sqrt(0.75)}),"bend hint chooses a three-dimensional plane");
    r.bend_hint={0,-1,0};
    expect(animation::solve_two_bone(r,s,error) && s.knee.y<0,"pole chooses bend side");
    r.target={5,0,0};
    expect(animation::solve_two_bone(r,s,error) && s.target_clamped && near(s.knee,{1,0,0}) && near(s.end,{2,0,0}),"outer reach does not stretch");
    r.upper_length=2;r.lower_length=1;r.target={0.1,0,0};
    expect(animation::solve_two_bone(r,s,error) && s.target_clamped && near(s.knee,{2,0,0}) && near(s.end,{1,0,0}),"inner reach with longer upper limb");
    r.upper_length=1;r.lower_length=2;
    expect(animation::solve_two_bone(r,s,error) && near(s.knee,{-1,0,0}) && near(s.end,{1,0,0}),"inner reach with longer lower limb");
    r={};r.target={};r.bend_hint={1,0,0};
    expect(animation::solve_two_bone(r,s,error) && s.used_fallback_direction && s.used_fallback_bend && near(s.end,{}) && near(s.knee,{0,1,0}),"coincident target and collinear pole have defined fallbacks");
    r={};r.upper_length=2;r.lower_length=1;r.target={};r.fallback_forward={0,0,-1};
    expect(animation::solve_two_bone(r,s,error) && s.target_clamped && near(s.end,{0,0,-1}) && near(s.knee,{0,0,-2}),
           "coincident unequal limb uses the explicit fallback direction");
    r={};r.root={64000,200,-64000};r.target=r.root+math::DVec3{1,0,0};r.bend_hint=r.root+math::DVec3{0,1,0};
    expect(animation::solve_two_bone(r,s,error) && near(s.knee-r.root,{.5,std::sqrt(.75),0}),"far-origin geometry matches local geometry");
    for(int i=1;i<=200;++i){
        r={};r.upper_length=0.1+i*.003;r.lower_length=.2+i*.002;
        r.target={std::sin(i*.17)*3,std::cos(i*.13)*2,std::sin(i*.11)};
        r.bend_hint={0,3,1};
        expect(animation::solve_two_bone(r,s,error) && std::abs(len(s.knee-s.root)-r.upper_length)<1e-8
            && std::abs(len(s.end-s.knee)-r.lower_length)<1e-8,"varied target preserves both segment lengths");
    }
    r={};r.target={1e-310,0,0};r.bend_hint={0,1e-310,0};
    expect(animation::solve_two_bone(r,s,error) && !s.used_fallback_direction && !s.used_fallback_bend
        && std::abs(len(s.knee-s.root)-1.0)<1e-8, "subnormal direction/hint normalise without reciprocal overflow");
    r={};r.upper_length=1e-6;r.lower_length=1e-6;r.target={1e-6,0,0};
    expect(animation::solve_two_bone(r,s,error), "minimum supported lengths solve");
    r={};r.upper_length=1e6;r.lower_length=1e6;r.target={1e6,0,0};
    expect(animation::solve_two_bone(r,s,error), "maximum supported lengths solve");
    const auto before=s;
    r={};r.lower_length=0;
    expect(!animation::solve_two_bone(r,s,error) && near(s.knee,before.knee) && !error.empty(),"invalid lengths leave output unchanged");
    r={};r.upper_length=1e6+1;
    expect(!animation::solve_two_bone(r,s,error),"oversized limb refused");
    r={};r.lower_length=1e-7;
    expect(!animation::solve_two_bone(r,s,error),"undersized limb refused");
    r={};r.target.x=std::numeric_limits<double>::quiet_NaN();
    expect(!animation::solve_two_bone(r,s,error),"nonfinite target refused");
    r={};r.target={};r.fallback_forward={};
    expect(!animation::solve_two_bone(r,s,error),"zero fallback direction refused when needed");
    r={};r.root={1e30,1e30,1e30};r.target=r.root;r.bend_hint=r.root;
    expect(!animation::solve_two_bone(r,s,error),"coordinate precision collapse refused");
    animation::Skeleton rig;
    for (int i=0;i<4;++i) { animation::Joint joint{};joint.name=std::to_string(i);rig.joints.push_back(joint); }
    rig.joints[1].parent=0;rig.joints[2].parent=1;rig.joints[3].parent=0;
    rig.joints[1].rest.translation={1,0,0};rig.joints[2].rest.translation={1,0,0};
    const auto pose=animation::rest_pose(rig);
    animation::Pose posed;
    animation::TwoBoneSolution geometry;
    expect(animation::pose_two_bone(rig,pose,0,1,2,{1,1,0},{0,2,0},posed,geometry,error),"pose reaches a bent target");
    std::vector<math::Mat4> fk;
    animation::global_transforms(rig,posed,fk);
    if (fk.size()==4) {
        expect(std::abs(fk[2].m[3][0]-1.F)<1e-4 && std::abs(fk[2].m[3][1]-1.F)<1e-4,"applied rotations reach target in FK");
        expect(posed[1].translation==pose[1].translation && posed[2].rotation.w==pose[2].rotation.w
            && posed[3].rotation.w==pose[3].rotation.w,"translations/end/sibling rotations preserved");
    }
    auto alias=pose;
    expect(animation::pose_two_bone(rig,alias,0,1,2,{-2,0,0},{0,1,0},alias,geometry,error),"aliasing and antiparallel corrections work");
    expect(animation::pose_two_bone(rig,pose,0,1,2,{-1.999999,0.001,0},{0,0,1},posed,geometry,error),
           "near-antiparallel targets preserve the small rotation difference");
    const auto previous=alias;
    auto bad_pose=pose;bad_pose[1].scale={2,1,1};
    const auto previous_geometry=geometry;
    expect(!animation::pose_two_bone(rig,bad_pose,0,1,2,{1,1,0},{0,1,0},alias,geometry,error)
        && alias[0].rotation.w==previous[0].rotation.w && near(geometry.end,previous_geometry.end),"unsupported scale fails transactionally");
    expect(!animation::pose_two_bone(rig,pose,0,2,1,{1,1,0},{0,1,0},alias,geometry,error),"non-direct chain refused");
    auto rotated=rig;
    rotated.joints[0].rest.rotation=math::from_axis_angle({0,0,1},0.7F);
    expect(animation::pose_two_bone(rotated,animation::rest_pose(rotated),1,2,3,{1,1,0},{0,1,0},alias,geometry,error)==false,
           "sibling cannot masquerade as a chain");
    animation::Skeleton parented;
    for (int i=0;i<5;++i) { animation::Joint joint{};joint.name=std::to_string(i);parented.joints.push_back(joint); }
    parented.joints[0].rest.rotation=math::from_axis_angle({0,0,1},0.7F);
    parented.joints[0].rest.translation={4,1,0};
    parented.joints[1].parent=0;parented.joints[1].rest.translation={0.2F,0.1F,0};
    parented.joints[2].parent=1;parented.joints[2].rest.translation={1,0,0};
    parented.joints[3].parent=2;parented.joints[3].rest.translation={1,0,0};
    parented.joints[4].parent=0;
    expect(animation::pose_two_bone(parented,animation::rest_pose(parented),1,2,3,{4.5,2.2,0.3},{4,3,1},posed,geometry,error),
           "rotated translated ancestor is accounted for in local rotations");
    animation::global_transforms(parented,posed,fk);
    if (fk.size()==5) {
        expect(std::abs(fk[3].m[3][0]-4.5F)<1e-4 && std::abs(fk[3].m[3][1]-2.2F)<1e-4
            && std::abs(fk[3].m[3][2]-0.3F)<1e-4,"parented chain reaches the independent model-space target");
        expect(posed[0].rotation.w==parented.joints[0].rest.rotation.w && posed[4].rotation.w==1.F,
               "ancestor and sibling local rotations stay unchanged");
    }
    auto stretched=pose;stretched[1].translation={2,0,0};
    expect(!animation::pose_two_bone(rig,stretched,0,1,2,{1,1,0},{0,1,0},alias,geometry,error),"changed segment length refused");
    auto collapsed_rig=rig;collapsed_rig.joints[1].rest.translation={};
    expect(!animation::pose_two_bone(collapsed_rig,animation::rest_pose(collapsed_rig),0,1,2,{1,1,0},{0,1,0},alias,geometry,error),
           "collapsed rest segment refused");
    for (int i=0;i<32;++i) {
        const double angle=i*0.17;
        expect(animation::pose_two_bone(rig,pose,0,1,2,{std::cos(angle),std::sin(angle),0.3},{0,0,2},posed,geometry,error),
               "varied three-dimensional poses verify in FK");
    }
    return failures==0?0:1;
}
