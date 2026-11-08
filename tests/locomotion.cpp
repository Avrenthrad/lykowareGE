#include "animated_mannequin.hpp"
#include <lykoware/player.hpp>
#include <array>
#include <iostream>
#include <limits>
#include <cstdlib>
#include <new>

namespace { thread_local bool tracking = false; thread_local std::size_t allocations = 0; }
void* operator new(std::size_t size) {
    if(tracking){++allocations;}
    if(auto* p=std::malloc(size==0?1:size)){return p;}
    throw std::bad_alloc();
}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
void operator delete[](void* p,std::size_t) noexcept{std::free(p);}

int main() {
    using locomotion::State;
    namespace a=lykoware::animation; namespace p=lykoware::physics;
    int failures=0;
    auto expect=[&](bool ok,const char* why){if(!ok){std::cerr<<"FAIL: "<<why<<'\n';++failures;}};
    locomotion::Selector selector;
    selector.advance(0,0,true,0);expect(selector.state()==State::Idle,"initial grounded state is idle, not landing");
    selector.advance(.01F,.25F,true,0);expect(selector.state()==State::Walk,"walk entry");
    selector.advance(.01F,.18F,true,0);expect(selector.state()==State::Walk,"walk hysteresis");
    selector.advance(.01F,.1F,true,0);expect(selector.state()==State::Idle,"idle return");
    selector.advance(.01F,5.1F,true,0);expect(selector.state()==State::Run,"run entry above normal walk speed");
    selector.advance(.01F,4.7F,true,0);expect(selector.state()==State::Run,"run hysteresis");
    selector.advance(.01F,4.F,true,0);expect(selector.state()==State::Walk,"run exit");
    selector.advance(.01F,4.F,false,5);expect(selector.state()==State::Jump,"rising input jumps");
    selector.advance(.01F,4.F,false,-.1F);expect(selector.state()==State::Jump,"apex hysteresis");
    selector.advance(.01F,4.F,false,-.2F);expect(selector.state()==State::Fall,"descending input falls");
    selector.advance(.01F,4.F,true,0);expect(selector.state()==State::Land,"observed air to ground lands");
    selector.advance(.1F,4.F,true,0);expect(selector.state()==State::Land,"landing holds");
    selector.advance(.09F,4.F,true,0);expect(selector.state()==State::Walk,"landing releases into movement");
    selector.advance(.01F,0,false,-1);selector.advance(.01F,0,true,0);selector.advance(.01F,0,false,3);
    expect(selector.state()==State::Jump,"new jump interrupts landing");
    const auto old_time=selector.seconds();selector.advance(std::numeric_limits<float>::quiet_NaN(),0,false,0);
    expect(selector.seconds()==old_time,"invalid drawing time does not advance state clock");
    const auto asset=training_mannequin::make();std::string error;
    for(const auto* clip : {&asset.idle,&asset.walk,&asset.run,&asset.jump,&asset.fall,&asset.land}) {
        expect(a::valid_clip(*clip,asset.skeleton,error),"six procedural clips validate");
    }
    training_mannequin::Playback playback;
    playback.advance(asset,0,0);const auto initial=playback.blended;
    playback.advance(asset,.01F,7);
    expect(playback.selector.state()==State::Run&&playback.blended[5].rotation.x==initial[5].rotation.x,"state change begins at last visible pose");
    for(int i=0;i<20;++i){playback.advance(asset,.01F,7);}
    expect(!playback.used_fallback&&std::abs(playback.blended[5].rotation.x)>0,"procedural run animates");
    const auto moving=playback.blended;
    playback.advance(asset,.01F,7,false,5);
    expect(playback.selector.state()==State::Jump&&playback.blended[5].rotation.x==moving[5].rotation.x,"air transition preserves visible continuity");
    auto imported=asset;imported.run={};imported.jump={};imported.fall={};imported.land={};
    training_mannequin::Playback fallback;
    fallback.advance(imported,0,7);expect(fallback.used_fallback,"missing run explicitly falls back to walk");
    fallback.advance(imported,.01F,0,false,-1);expect(fallback.used_fallback,"missing fall explicitly falls back to idle");
    fallback.reset();fallback.advance(imported,0,0,true,0);
    expect(fallback.selector.state()==State::Idle&&!fallback.used_fallback,"respawn clears old airborne/landing observation");
    training_mannequin::Playback reused;reused.advance(asset,0,0);
    tracking=true;auto counter_control=asset;tracking=false;
    expect(allocations>0&&counter_control.skeleton.joints.size()==7,"allocation counter detects setup copies");
    allocations=0;tracking=true;
    for(int i=0;i<1000;++i){
        const int phase=i%200;
        reused.advance(asset,1.F/60.F,phase<40?0.F:phase<80?4.F:7.F,
            phase<120||phase>=180,phase<150?4.F:-4.F);
    }
    tracking=false;expect(allocations==0,"all six-state playback updates reuse ordinary C++ heap storage");
    std::cout<<"playback_updates=1000 allocations="<<allocations<<'\n';
    // Real controller/physics observations must reach all six visual states.
    auto world=p::World::create({},error);if(!world){std::cerr<<error;return 1;}
    p::BodyDesc floor{};floor.motion=p::MotionType::Static;floor.shape=p::BoxShape{{100,.5F,100}};floor.position={0,-.5,0};
    if(!world->create_body(floor,error)){std::cerr<<error;return 1;}
    auto player=lykoware::PlayerCharacter::create(*world,{},error);if(!player){std::cerr<<error;return 1;}
    std::array<bool,6> visited{};training_mannequin::Playback actual;
    for(int tick=0;tick<300;++tick){
        lykoware::PlayerIntent intent;
        intent.forward=tick>=60&&tick<230?1.F:0.F;intent.run=tick>=120&&tick<230;intent.jump=tick==180;
        player->steer(intent);player->before_step(1.F/60.F);expect(world->step(1.F/60.F),"physics step");
        actual.advance_character(asset,1.F/60.F,player->state());
        visited[static_cast<std::size_t>(actual.selector.state())]=true;
    }
    for(std::size_t i=0;i<visited.size();++i){expect(visited[i],locomotion::name(static_cast<State>(i)));}
    return failures==0?0:1;
}
