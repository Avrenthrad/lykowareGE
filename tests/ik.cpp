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
    return failures==0?0:1;
}
