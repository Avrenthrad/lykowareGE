#include <lykoware/character_asset.hpp>
#include <lykoware/player.hpp>
#include "animated_mannequin.hpp"
#include "foot_placement.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>

int main(){
    namespace c=lykoware::character;namespace a=lykoware::animation;namespace m=lykoware::math;
    int failures=0;auto expect=[&](bool ok,const char* why){if(!ok){std::cerr<<"FAIL: "<<why<<'\n';++failures;}};std::string error;
    c::Profile profile;
    expect(c::decode_profile(R"({"version":1,"model":"hero.glb","height":2,"yaw_degrees":90,"capsule_height":2,"capsule_radius":0.3,"eye_height":1.7,"run_clip":"Run"})","assets",profile,error),"decode shared alignment profile");
    expect(profile.model==std::filesystem::path("assets")/"hero.glb"&&profile.clips[2]=="Run","relative paths and state names");
    const auto old=profile;
    for(const char* bad:{"{}",R"({"version":1,"version":1,"model":"x"})",R"({"version":1,"model":"x","yaw_degrees":181})",R"({"version":1,"model":"x","capsule_radius":1})",R"({"version":1,"model":"x","capsule_height":2,"eye_height":2})",R"({"version":1,"model":"x","run_clip":false})",R"({"version":1,"model":"x","foot_ik":true})",R"({"version":1,"model":"x","typo":1})"}){
        expect(!c::decode_profile(bad,"",profile,error)&&profile.model==old.model&&profile.height==old.height,"invalid profile is transactional");
    }
    expect(!c::decode_profile(std::string(65537,' '),"",profile,error),"profile size bounded");
    {   // Optional locomotion object (ADR 0074): the motor profile without its own version.
        c::Profile plain;expect(c::decode_profile(R"({"version":1,"model":"x"})","",plain,error)&&!plain.locomotion,"locomotion absent means nullopt");
        c::Profile loco;
        const std::string full=R"({"version":1,"model":"x","locomotion":{"agility":0.8,"gaits":{"walk":1.5,"jog":3,"run":5.5,"sprint":8},"gait_margin":0.2,"air_control":3,"max_lean":0.3,"lean_seconds":0.2,"strafe_scale":0.8,"backward_scale":0.6,"heavy":{"acceleration":7},"sharp":{"braking":40},"overrides":{"lean":0.05},"variants":{"combat":{"agility":0.9,"overrides":{"acceleration":30}},"sneak":{}}}})";
        expect(c::decode_profile(full,"",loco,error)&&loco.locomotion.has_value(),"profile with locomotion decodes");
        if(loco.locomotion){
            const auto& m=*loco.locomotion;
            expect(m.base.agility==.8F&&m.gait_speeds==std::array<float,4>{1.5F,3.F,5.5F,8.F},"locomotion agility and gaits are copied");
            expect(m.gait_margin==.2F&&m.air_control==3.F&&m.max_lean==.3F&&m.lean_seconds==.2F,"locomotion scalars are copied");
            expect(m.strafe_scale==.8F&&m.backward_scale==.6F,"locomotion strafe and backward scales are copied");
            expect(m.heavy.acceleration==7.F&&m.sharp.braking==40.F&&m.base.overrides[9]==.05F,"locomotion tuning and overrides are copied");
            expect(m.variants.size()==2&&m.variants[0].name=="combat"&&m.variants[0].agility==.9F&&m.variants[0].overrides[0]==30.F&&m.variants[1].name=="sneak","locomotion variants are copied");
        }
        expect(c::decode_profile(R"({"version":1,"model":"x","locomotion":{}})","",plain,error)&&plain.locomotion.has_value()&&plain.locomotion->base.agility==.5F,"an empty locomotion object means defaults");
        const auto before_model=loco.model;const auto before_loco=loco.locomotion;
        for(const char* bad:{R"("locomotion":3)",R"("locomotion":[])",R"("locomotion":null)",R"("locomotion":{"agility":"high"})",R"("locomotion":{"typo":1})",R"("locomotion":{"gaits":{"amble":1}})",
                R"("locomotion":{"version":1})",R"("locomotion":{"agility":2})",R"("locomotion":{"gait_margin":0.9})",R"("locomotion":{"strafe_scale":2})",R"("locomotion":{"gaits":{"walk":9,"jog":3}})",
                R"("locomotion":{"agility":0.5,"agility":0.6})",R"("locomotion":{"variants":{"a":{"overrides":{"lean":1,"lean":1}}}})"}){
            c::Profile target=loco;error.clear();
            expect(!c::decode_profile(std::string(R"({"version":1,"model":"y",)")+bad+"}","",target,error)&&target.model==before_model&&target.locomotion.has_value()&&target.locomotion->base.agility==before_loco->base.agility,bad);
            const bool scoped=error.rfind("Character profile locomotion: ",0)==0;
            if(!scoped){std::cerr<<"error not scoped: "<<error<<" for "<<bad<<'\n';}
            expect(scoped,"locomotion errors name the locomotion object");
        }
        expect(!c::decode_profile(R"({"version":1,"model":"x","locomotion":{"agility":0.5,"agility":0.6}})","",plain,error)&&error=="Character profile locomotion: Duplicate locomotion profile key","duplicate key inside locomotion is refused with its own message");
        expect(!c::decode_profile(R"({"version":1,"model":"x","bones":{"pelvis":"a","pelvis":"b"}})","",plain,error)&&error=="Duplicate character bone key","a duplicate bone key keeps its message");
        expect(!c::decode_profile(R"({"version":1,"model":"x","locomotion":{},"locomotion":{}})","",plain,error)&&error=="Duplicate character profile key","a duplicate locomotion key at top level is refused");
        // Authored clip speeds (ADR 0074): optional, positive, walk slower than run.
        expect(c::decode_profile(R"({"version":1,"model":"x"})","",plain,error)&&!plain.walk_clip_speed&&!plain.run_clip_speed,"clip speeds default to absent");
        expect(c::decode_profile(R"({"version":1,"model":"x","walk_clip_speed":1.4,"run_clip_speed":4.5})","",plain,error)&&plain.walk_clip_speed==1.4F&&plain.run_clip_speed==4.5F,"clip speeds are copied");
        expect(c::decode_profile(R"({"version":1,"model":"x","run_clip_speed":3})","",plain,error)&&!plain.walk_clip_speed&&plain.run_clip_speed==3.F,"one clip speed may be given alone");
        for(const char* bad:{R"("walk_clip_speed":0)",R"("walk_clip_speed":-1)",R"("run_clip_speed":"fast")",R"("run_clip_speed":null)",R"("walk_clip_speed":25)",
                R"("walk_clip_speed":4,"run_clip_speed":2)",R"("walk_clip_speed":3,"run_clip_speed":3)"}){
            c::Profile target=plain;
            expect(!c::decode_profile(std::string(R"({"version":1,"model":"z",)")+bad+"}","",target,error)&&target.model==plain.model,bad);
        }
        // The 64 KiB bound covers the nested object.
        expect(!c::decode_profile(std::string(R"({"version":1,"model":"x","locomotion":{"variants":{)")+std::string(70000,' ')+"}}}","",plain,error),"oversize locomotion profile refused");
    }
    c::Alignment aligned;const lykoware::Bounds box{{10,-4,20},{14,4,26}};
    expect(c::align(box,2,90,aligned,error)&&aligned.scale==.25F,"yaw and visual height fit");
    const auto direction=m::rotate(aligned.rotation,m::Vec3{0,0,-1});
    expect(std::abs(direction.x+1.F)<1e-5F&&std::abs(direction.z)<1e-5F,"yaw actually rotates authored forward direction");
    const auto feet=m::rotate(aligned.rotation,m::Vec3{12,-4,23}*aligned.scale)+aligned.offset;
    const auto head=m::rotate(aligned.rotation,m::Vec3{12,4,23}*aligned.scale)+aligned.offset;
    expect(m::length(feet)<1e-5F&&m::length(head-m::Vec3{0,2,0})<1e-5F,"yaw preserves centred feet and height");
    const auto before=aligned;expect(!c::align(box,0,90,aligned,error)&&aligned.offset==before.offset,"bad fit leaves output unchanged");
    c::Capsule capsule;expect(c::capsule(old,capsule,error)&&std::abs(2*(capsule.cylinder_half_height+capsule.radius)-2)<1e-6F&&capsule.eye_height==1.7F,"capsule and camera height align");
    c::Profile visual_only;visual_only.height=4.F;expect(c::capsule(visual_only,capsule,error)&&capsule.height==1.8F&&capsule.eye_height==1.6F,"legacy visual height does not resize physics");
    auto tiny=visual_only;tiny.capsule_height=.1F;expect(c::capsule(tiny,capsule,error)&&capsule.radius<.05F&&capsule.eye_height<.1F,"small character derives valid capsule/eye");
    auto bad_capsule=tiny;bad_capsule.eye_height=.2F;const auto old_capsule=capsule;
    expect(!c::capsule(bad_capsule,capsule,error)&&capsule.height==old_capsule.height,"invalid camera transaction");
    const auto actor=m::from_axis_angle({0,1,0},.8F);
    const auto local_offset=m::rotate(m::conjugate(before.rotation),before.offset);
    const foot_placement::Placement placement{{64000,3,-64000},actor*before.rotation,before.scale,{local_offset.x,local_offset.y,local_offset.z}};
    const m::Vec3 model{11,-2,24};
    const auto from_ik=foot_placement::to_world(placement,{model.x,model.y,model.z});
    const auto rendered=m::DVec3{64000,3,-64000}+m::rotate(actor,before.offset)+m::rotate(actor*before.rotation,model*before.scale);
    expect(foot_placement::norm(from_ik-rendered)<1e-5,"render and terrain IK agree after facing/yaw/fitting at far origin");
    const auto source=training_mannequin::make();
    lykoware::gltf::SkinnedModel asset;asset.model.mesh=source.mesh;asset.skeleton=source.skeleton;asset.joints=source.joints;asset.weights=source.weights;asset.clips={source.idle,source.walk};
    c::Inspection inspection;expect(c::inspect(asset,{},inspection,error)&&inspection.vertices==source.mesh.positions.size(),"character mesh/skin/clip validation");
    const auto old_vertices=inspection.vertices;auto broken=asset;broken.weights[0].x=-1;
    expect(!c::inspect(broken,{},inspection,error)&&inspection.vertices==old_vertices,"invalid skin data is transactional");
    auto target=source.skeleton;target.joints[0].rest.rotation=m::from_axis_angle({0,1,0},std::numbers::pi_v<float>*.5F);
    for(auto& joint:target.joints){joint.rest.translation=joint.rest.translation*1.5F;}
    expect(c::compare_rigs(source.skeleton,target).compatible,"compatible named rigs may differ in length and root basis");
    target.joints[4].name=target.joints[3].name;expect(!c::compare_rigs(source.skeleton,target).compatible,"duplicate names diagnosed");
    target=source.skeleton;target.joints[2].parent=std::uint16_t{0};expect(!c::compare_rigs(source.skeleton,target).compatible,"parent mapping diagnosed");
    target=source.skeleton;target.joints[2].rest.rotation=m::from_axis_angle({1,0,0},.4F);expect(!c::compare_rigs(source.skeleton,target).compatible,"unsupported rest axes diagnosed");
    target=source.skeleton;target.joints[2].rest.translation={1,0,0};expect(!c::compare_rigs(source.skeleton,target).compatible,"rest direction mismatch diagnosed");
    target=source.skeleton;target.joints[1].rest.scale={2,1,1};expect(!c::compare_rigs(source.skeleton,target).compatible,"scaled rigs diagnosed");
    target=source.skeleton;target.joints[0].rest.translation={1e30F,1e30F,1e30F};expect(!c::describe_rig(target).nonzero_links,"precision-collapsed links diagnosed");
    lykoware::PlayerConfig config;c::Capsule body;expect(c::capsule(old,body,error),"physical alignment setup");config.body.radius=body.radius;config.body.cylinder_half_height=body.cylinder_half_height;config.eye_height=body.eye_height;
    auto world=lykoware::physics::World::create({},error);if(!world){std::cerr<<error;return 1;}
    auto player=lykoware::PlayerCharacter::create(*world,config,error);expect(player&&std::abs(player->view(0).target.y-body.eye_height)<1e-5,"actual controller camera uses aligned pivot");
    return failures==0?0:1;
}
