#include <lykoware/animation.hpp>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>

namespace { thread_local int fail_after = -1; thread_local bool counting = false; thread_local std::size_t allocations = 0; }
void* operator new(std::size_t size) {
    if (counting) { ++allocations; }
    if (fail_after >= 0 && fail_after-- == 0) { throw std::bad_alloc(); }
    if (auto* p = std::malloc(size == 0 ? 1 : size)) { return p; }
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

int main() {
    namespace a = lykoware::animation; namespace m = lykoware::math;
    int failures = 0;
    const auto expect = [&](bool ok,const char* why){if(!ok){std::cerr<<"FAIL: "<<why<<'\n';++failures;}};
    std::string error;
    a::Pose start(1), target(1), out(1);
    target[0].translation={10,0,0}; target[0].rotation=m::from_axis_angle({0,1,0},1.5707963268F);
    a::PoseCrossfade fade;
    expect(!fade.begin(1,error),"begin requires initialization");
    expect(fade.reset(start,error)&&fade.begin(1,error)&&fade.advance(target,0,out,error)&&out[0].translation.x==0,"transition starts at source");
    expect(fade.advance(target,.5F,out,error)&&out[0].translation.x==5&&fade.progress()==.5F,"halfway position and progress");
    const auto direction=m::rotate(out[0].rotation,{1,0,0});
    expect(std::abs(direction.x-std::sqrt(.5F))<1e-5F,"rotation follows halfway shortest arc");
    auto interruption=target; interruption[0].translation={-10,0,0};
    expect(fade.begin(1,error)&&fade.advance(interruption,0,out,error)&&out[0].translation.x==5,"interrupt captures visible blend without a jump");
    expect(fade.advance(interruption,.5F,out,error)&&out[0].translation.x==-2.5F,"interrupted transition uses captured visible source");
    const auto previous=out[0]; const auto progress=fade.progress();
    auto bad=target; bad[0].rotation={0,0,0,0};
    expect(!fade.advance(bad,.1F,out,error)&&out[0].translation==previous.translation&&fade.progress()==progress,"bad rotation preserves output/time");
    for(float seconds : {-1.F,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
        expect(!fade.advance(target,seconds,out,error)&&out[0].translation==previous.translation&&fade.progress()==progress,"bad delta preserves output/time");
        expect(!fade.begin(seconds,error)&&fade.progress()==progress,"bad begin preserves progress");
    }
    expect(!fade.advance(target,.1F,std::span<a::JointPose>{},error)&&fade.progress()==progress,"wrong output size refused");
    expect(!fade.reset({},error)&&fade.progress()==progress,"bad reset preserves transition");
    expect(!fade.begin(3601.F,error)&&fade.progress()==progress,"excessive duration refused");
    for(int failure=0;failure<3;++failure) {
        fail_after=failure; bool threw=false;
        try { (void)fade.reset(start,error); } catch(const std::bad_alloc&) { threw=true; }
        fail_after=-1;
        expect(threw&&fade.progress()==progress,"reset allocation failure preserves old transition");
        expect(fade.advance(interruption,0,out,error)&&out[0].translation==previous.translation,"reset failure preserves visible pose");
    }
    expect(fade.advance(interruption,1,out,error)&&out[0].translation.x==-10&&!fade.transitioning(),"finish reaches endpoint");
    auto extreme=target;extreme[0].translation.x=std::numeric_limits<float>::max();
    expect(fade.begin(0,error)&&fade.advance(extreme,0,out,error)&&out[0].translation.x==extreme[0].translation.x,"zero duration cuts exactly");
    extreme[0].translation.x=-std::numeric_limits<float>::max();
    expect(fade.begin(1,error)&&fade.advance(extreme,.5F,out,error)&&std::isfinite(out[0].translation.x),"finite extremes interpolate without overflow");
    auto negative_scale=start;negative_scale[0].scale={-1,1,1};
    expect(fade.reset(start,error)&&fade.begin(1,error),"scale-crossing setup");
    expect(fade.advance(start,0,out,error),"scale setup output");
    expect(!fade.advance(negative_scale,.5F,out,error)&&fade.progress()==0&&out[0].scale.x==1,"zero intermediate scale fails transactionally");
    auto opposite=start;opposite[0].rotation={0,0,0,-1};
    expect(fade.advance(opposite,.5F,out,error)&&std::abs(out[0].rotation.w)> .999F,"quaternion sign selects shortest arc");
    a::PoseCrossfade at30,at144;
    expect(at30.reset(start,error)&&at144.reset(start,error)&&at30.begin(1,error)&&at144.begin(1,error),"partition setup");
    a::Pose other(1);
    for(int i=0;i<30;++i){expect(at30.advance(target,1.F/30.F,out,error),"30 Hz update");}
    for(int i=0;i<144;++i){expect(at144.advance(target,1.F/144.F,other,error),"144 Hz update");}
    expect(std::abs(out[0].translation.x-other[0].translation.x)<1e-5F,"equal elapsed time matches across frame rates");
    a::Pose two(2),overlap(3);overlap[0].translation.x=10;overlap[1].translation.x=20;
    expect(fade.reset(two,error)&&fade.begin(1,error)&&fade.advance(std::span(overlap).first(2),.5F,std::span(overlap).subspan(1),error)
        &&overlap[1].translation.x==5&&overlap[2].translation.x==10,"overlapping target/output supported");
    expect(fade.reset(start,error),"allocation setup");
    bool ok=true; allocations=0;counting=true;
    for(int i=0;i<1000;++i){ok=fade.begin(.1F,error)&&fade.advance(target,.01F,out,error)&&ok;}
    counting=false;
    expect(ok&&allocations==0,"begin/advance reuse all ordinary C++ heap storage");
    std::cout<<"crossfade_updates=1000 allocations="<<allocations<<'\n';
    a::Pose maximum(a::max_joints), maximum_out(a::max_joints);
    maximum.back().translation = {9,8,7};
    expect(fade.reset(maximum,error)&&fade.advance(maximum,0,maximum_out,error)
        &&maximum_out.back().translation==maximum.back().translation,"maximum joint pose round-trips completely");
    maximum.emplace_back();
    expect(!fade.reset(maximum,error),"oversized pose refused");
    return failures==0?0:1;
}
