#include <lykoware/animation_graph.hpp>
#include <lykoware/retarget.hpp>
#include "animated_mannequin.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <numbers>
namespace {thread_local bool tracking=false;thread_local std::size_t allocations=0;}
void* operator new(std::size_t n){if(tracking){++allocations;}if(auto* p=std::malloc(n?n:1)){return p;}throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
int main(){
    using namespace lykoware;namespace a=animation;int failures=0;auto check=[&](bool b,const char* why){if(!b){std::cerr<<"FAIL: "<<why<<'\n';++failures;}};std::string error;
    a::Skeleton skeleton; a::Joint root;root.name="root";a::Joint hand;hand.name="hand";hand.parent=std::uint16_t{0};hand.rest.translation={0,1,0};hand.inverse_bind=math::translation({0,-100,0});skeleton.joints={root,hand};
    a::Clip idle;idle.name="idle";a::Channel move;move.joint=0;move.times={0,1};move.values={{0,0,0,0},{2,0,0,0}};idle.channels={move};a::Clip walk=idle;walk.name="walk";walk.channels[0].values[1].x=4;
    a::Graph graph;const std::string text=R"({"version":1,"parameters":[{"name":"speed"}],"states":[{"name":"idle","clip":"idle","events":[{"name":"entry","seconds":0},{"name":"step","seconds":0.5},{"name":"end","seconds":1}]},{"name":"walk","clip":"walk","fade":0.25}],"edges":[{"from":0,"to":1,"conditions":[{"parameter":0,"op":">","value":1}]},{"from":1,"to":0,"conditions":[{"parameter":0,"op":"<","value":0.5}]}],"sockets":[{"name":"weapon","joint":"hand","translation":[1,0,0]}]})";
    check(a::decode_graph(text,graph,error),"authored graph decode");auto player=a::GraphPlayer::create(skeleton,{idle,walk},graph,error);if(!player){std::cerr<<error;return 1;}
    std::array<float,1> params{{0}};check(player->step(.5F,params,error),"first half step");check(player->events().size()==2&&player->events()[0].event==0&&player->events()[1].event==1,"entry zero and half event");check(std::abs(player->pose()[0].translation.x-1)<1e-6,"sampled pose");check(std::abs(player->sockets()[0].m[3][0]-2)<1e-6&&player->sockets()[0].m[3][1]==1,"socket uses posed global joint, not inverse bind");
    check(player->step(.5F,params,error),"exact wrap step");check(player->events().size()==2,"endpoint and next-cycle zero both emitted at wrap");check(player->step(0,params,error)&&player->events().empty(),"no duplicate events on zero dt");
    params[0]=2;check(player->step(.125F,params,error)&&player->state()==1,"authored transition");check(std::abs(player->pose()[0].translation.x-.25F)<1e-6,"crossfade advances half way from visible wrap pose");
    const auto held=player->pose()[0].translation;params[0]=0;check(player->step(0,params,error)&&player->pose()[0].translation==held,"interruption captures visible pose");
    const auto age=player->age();const auto state=player->state();params[0]=std::numeric_limits<float>::quiet_NaN();check(!player->step(.1F,params,error)&&player->age()==age&&player->state()==state&&player->pose()[0].translation==held,"bad step preserves state and pose");params[0]=0;
    graph.states[0].rate=16;auto overflow=a::GraphPlayer::create(skeleton,{idle,walk},graph,error);check(overflow&&!overflow->step(60,params,error)&&overflow->age()==0,"event overflow refuses before advancing state");graph.states[0].rate=1;
    auto bad=graph;bad.states[0].clip="missing";check(!a::GraphPlayer::create(skeleton,{idle,walk},bad,error),"missing clip refused");bad=graph;bad.edges[0].conditions[0].parameter=99;check(!a::GraphPlayer::create(skeleton,{idle,walk},bad,error),"bad parameter refused");bad=graph;bad.sockets[0].joint="absent";check(!a::GraphPlayer::create(skeleton,{idle,walk},bad,error),"missing socket joint refused");
    a::Graph unchanged=graph;check(!a::decode_graph(R"({"version":1,"version":1,"states":[]})",unchanged,error)&&unchanged.states.size()==2,"duplicate JSON keys refused transactionally");check(!a::decode_graph(R"({"version":1,"states":[{"name":"x","clip":"idle","oops":1}]})",unchanged,error),"unknown graph field refused");
    a::Clip scaled=idle;scaled.channels[0].target=a::Target::Scale;scaled.channels[0].values={{1,1,1,0},{2,2,2,0}};auto scale_player=a::GraphPlayer::create(skeleton,{scaled,walk},graph,error);check(scale_player&&!scale_player->step(.5F,params,error)&&scale_player->age()==0,"unsupported animated scale rejected transactionally");
    a::PoseCrossfade fade;auto pose=a::rest_pose(skeleton);check(fade.reset(pose,error),"fade setup");auto invalid=pose;invalid[0].scale={-1,-1,-1};auto output=pose;check(!fade.retarget(invalid,.5F,1.F,output,error)&&output[0].scale==pose[0].scale&&fade.progress()==1,"atomic restart rejects zero intermediate scale");
    auto target=skeleton;target.joints[0].rest.rotation=math::from_axis_angle({0,1,0},std::numbers::pi_v<float>*.5F);target.joints[0].rest.translation={10,0,0};target.joints[1].rest.translation={0,2,0};auto plan=a::Retarget::create(skeleton,target,2,error);if(!plan){std::cerr<<error;return 1;}
    pose[0].translation={1,0,0};pose[1].rotation=math::from_axis_angle({1,0,0},.4F);output.resize(2);check(plan->apply(pose,output,error),"supported retarget transfer");check(std::abs(output[0].translation.z+2)<1e-5&&std::abs(output[0].translation.x-10)<1e-5&&output[1].translation.y==2,"root basis/scale and target bone length");check(std::abs(output[1].rotation.x-pose[1].rotation.x)<1e-6,"local rotation transferred");
    const auto saved=output[0].translation;pose[1].translation.x=1;check(!plan->apply(pose,output,error)&&output[0].translation==saved,"dynamic bone stretch refused without output change");pose[1].translation.x=0;
    auto incompatible=target;incompatible.joints[1].rest.translation={2,0,0};check(!a::Retarget::create(skeleton,incompatible,1,error),"different rest direction refused");incompatible=target;incompatible.joints[1].name="unknown";check(!a::Retarget::create(skeleton,incompatible,1,error),"unmapped bones refused");check(!a::Retarget::create(skeleton,target,0,error),"zero root scale refused");
    auto aliased=pose;check(plan->apply(aliased,aliased,error)&&aliased[1].translation.y==2,"retarget input/output aliases safely");
    skeleton.joints[1].name="mutated";target.joints.clear();check(plan->apply(pose,output,error),"prepared retarget owns source and target snapshots");
    allocations=0;tracking=true;bool success=true;for(int i=0;i<1000;++i){params[0]=(i%2)?2.F:0.F;success=player->step(.01F,params,error)&&plan->apply(pose,output,error)&&success;}tracking=false;check(success&&allocations==0,"1000 graph/events/socket/retarget updates allocate no ordinary C++ storage");std::cout<<"runtime_updates=1000 allocations="<<allocations<<'\n';
    auto asset=training_mannequin::make();a::Graph sample_graph;sample_graph.parameters={{"speed",0}};sample_graph.states={{"idle",asset.idle.name},{"walk",asset.walk.name}};sample_graph.edges={{0,1,0,{{0,a::Compare::Greater,1}}}};asset.graph=std::make_shared<a::Graph>(sample_graph);asset.graph_clips={asset.idle,asset.walk};training_mannequin::Playback playback;physics::CharacterState observation;observation.ground=physics::GroundState::OnGround;observation.velocity={2,0,0};playback.advance_character(asset,.1F,observation);check(playback.graph_player&&playback.graph_player->state()==1&&playback.matrices.size()==7,"actual character adapter consumes authored graph and skinning");
    return failures?1:0;
}
