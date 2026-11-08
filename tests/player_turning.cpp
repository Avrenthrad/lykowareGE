#include <lykoware/player.hpp>
#include <lykoware/character_asset.hpp>
#include "animated_mannequin.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>

int main(){
    using namespace lykoware;constexpr float pi=std::numbers::pi_v<float>;
    int failures=0;auto expect=[&](bool ok,const char* why){if(!ok){std::cerr<<"FAIL: "<<why<<'\n';++failures;}};
    std::string error;auto world=physics::World::create({},error);if(!world){std::cerr<<error;return 1;}
    PlayerConfig config;config.max_turn_rate=pi;
    auto player=PlayerCharacter::create(*world,config,error);if(!player){std::cerr<<error;return 1;}
    player->steer(PlayerIntent{.right=1.F});player->before_step(.1F);
    expect(std::abs(player->facing()+pi*.1F)<1e-5F,"facing advances by bounded shortest yaw");
    expect(std::abs(player->drawn_facing(.5F)+pi*.05F)<1e-5F,"drawn yaw interpolates between fixed ticks");
    player->steer({});for(int i=0;i<10;++i){player->before_step(.1F);}
    expect(std::abs(player->facing()+pi*.5F)<1e-5F,"idle continues last requested turn to completion");
    auto wrapped=PlayerCharacter::create(*world,config,error);if(!wrapped){return 1;}
    wrapped->steer(PlayerIntent{.forward=1.F,.yaw=pi-.01F});wrapped->before_step(1.F);
    const float previous=wrapped->facing();wrapped->steer(PlayerIntent{.forward=1.F,.yaw=.02F});wrapped->before_step(.01F);
    expect(std::abs(std::remainder(wrapped->facing()-previous,2*pi))<.021F,"wrap crosses pi using short path");
    expect(std::abs(wrapped->drawn_facing(.5F))>3.13F,"interpolation does not turn through zero at wrap");
    expect(wrapped->drawn_facing(-1)==previous&&wrapped->drawn_facing(2)==wrapped->facing(),"alpha clamps to tick endpoints");
    expect(wrapped->drawn_facing(std::numeric_limits<float>::quiet_NaN())==wrapped->facing(),"invalid alpha uses latest yaw");
    const auto hash=world->state_hash();(void)wrapped->drawn_facing(.3F);(void)wrapped->view(.01F,.3F);
    auto asset=training_mannequin::make();asset.idle.channels.clear();
    animation::Channel root;root.joint=0;root.target=animation::Target::Translation;root.times={0,1};root.values={{0,0,0,0},{10,0,0,0}};asset.idle.channels.push_back(root);
    training_mannequin::Playback visual;for(int i=0;i<55;++i){visual.advance(asset,1.F/60.F,0);}
    expect(world->state_hash()==hash,"turn interpolation, camera and translated-root animation never move physics");
    expect(visual.blended[0].translation.x>9.F,"root track remains supported as visual pose data");
    PlayerConfig legacy;auto instant=PlayerCharacter::create(*world,legacy,error);if(!instant){return 1;}
    instant->steer(PlayerIntent{.right=1.F});instant->before_step(.01F);expect(std::abs(instant->facing()+pi*.5F)<1e-5F,"zero rate preserves legacy instant facing");
    config.max_turn_rate=-1;expect(!PlayerCharacter::create(*world,config,error),"negative rate rejected");
    config.max_turn_rate=std::numeric_limits<float>::quiet_NaN();expect(!PlayerCharacter::create(*world,config,error),"nonfinite rate rejected");
    character::Profile profile;
    expect(character::decode_profile(R"({"version":1,"model":"hero.glb","motion_authority":"controller","turn_degrees_per_second":360})","",profile,error),"authority/turn profile parses");
    expect(!character::decode_profile(R"({"version":1,"model":"hero.glb","motion_authority":"animation"})","",profile,error),"unsupported animation authority refused explicitly");
    expect(!character::decode_profile(R"({"version":1,"model":"hero.glb","turn_degrees_per_second":-1})","",profile,error),"bad authored turn rate refused");
    return failures==0?0:1;
}
