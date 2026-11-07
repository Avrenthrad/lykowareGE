#include "foot_placement.hpp"
#include <cstdint>
#include <cmath>
#include <iostream>
#include <limits>

int main()
{
    namespace a=lykoware::animation;
    namespace m=lykoware::math;
    namespace p=lykoware::physics;
    using V=m::DVec3;
    int failures=0;
    auto expect=[&](bool ok,const char* reason){if(!ok){std::cerr<<"FAIL: "<<reason<<'\n';++failures;}};
    foot_placement::Placement actor{{64000,3,-64000},m::from_axis_angle({0,1,0},0.8F),1.8,{-.2,.3,.1}};
    const V point{.2,.8,-.3};
    const auto roundtrip=foot_placement::to_model(actor,foot_placement::to_world(actor,point));
    expect(foot_placement::valid(actor)&&foot_placement::norm(roundtrip-point)<1e-6,"far-origin rotated fitted placement round trips");
    auto bad_actor=actor;bad_actor.uniform_scale=0;
    expect(!foot_placement::valid(bad_actor),"zero scale refused");
    bad_actor=actor;bad_actor.rotation.w=2;
    expect(!foot_placement::valid(bad_actor),"nonunit actor orientation refused");
    a::Skeleton skeleton;
    for(const auto* name:rig_roles::names){a::Joint j{};j.name=name;skeleton.joints.push_back(j);}
    skeleton.joints[0].rest.translation={0,1.6F,0};
    skeleton.joints[1].parent=std::uint16_t{0};skeleton.joints[1].rest.translation={-.2F,0,0};
    skeleton.joints[2].parent=std::uint16_t{1};skeleton.joints[2].rest.translation={0,-.8F,0};
    skeleton.joints[3].parent=std::uint16_t{2};skeleton.joints[3].rest.translation={0,-.8F,0};
    skeleton.joints[4].parent=std::uint16_t{0};skeleton.joints[4].rest.translation={.2F,0,0};
    skeleton.joints[5].parent=std::uint16_t{4};skeleton.joints[5].rest.translation={0,-.8F,0};
    skeleton.joints[6].parent=std::uint16_t{5};skeleton.joints[6].rest.translation={0,-.8F,0};
    const rig_roles::Indices roles{0,1,2,3,4,5,6};
    const auto rest=a::rest_pose(skeleton);
    std::string error;
    foot_placement::Controller controller;
    expect(controller.configure(skeleton,roles,error),"supported mapped rig calibrates");
    auto invalid=skeleton;invalid.joints[2].rest.translation={};
    foot_placement::Controller refused;
    expect(!refused.configure(invalid,roles,error),"collapsed mapped limb refused");
    invalid=skeleton;invalid.joints[2].rest.scale={2,1,1};
    expect(!refused.configure(invalid,roles,error),"unsupported mapped scales refused");
    invalid=skeleton;invalid.joints[4].parent=std::uint16_t{3};
    expect(!refused.configure(invalid,roles,error),"interdependent leg branches refused");
    auto world=p::World::create({},error);
    if(!world){std::cerr<<error;return 1;}
    p::BodyDesc floor{};floor.motion=p::MotionType::Static;floor.shape=p::BoxShape{{5,.5F,5}};floor.position={0,-.5,0};
    const auto ground=world->create_body(floor,error);
    if(!ground){std::cerr<<error;return 1;}
    actor={};
    foot_placement::Controller range_check;
    expect(range_check.configure(skeleton,roles,error),"range-check controller calibrates");
    auto huge=actor;huge.feet.x=1e40;
    a::Pose rejected=rest;
    expect(!range_check.update(rest,huge,*world,1.F/60.F,true,rejected,error),"unrepresentable physics ray origin refused before query");
    const auto hash=world->state_hash();
    a::Pose output;
    for(int frame=0;frame<60;++frame){expect(controller.update(rest,actor,*world,1.F/60.F,true,output,error),"stationary contact update succeeds");}
    std::vector<m::Mat4> fk;
    a::global_transforms(skeleton,output,fk);
    if(fk.size()==7){expect(std::abs(fk[3].m[3][1]-.03F)<.001F&&std::abs(fk[6].m[3][1]-.03F)<.001F,"both feet approach surface clearance");}
    expect(world->state_hash()==hash,"foot probes/pose updates never mutate physics");
    foot_placement::Controller slower;
    expect(slower.configure(skeleton,roles,error),"comparison controller calibrates");
    a::Pose at30;
    for(int i=0;i<30;++i){expect(slower.update(rest,actor,*world,1.F/30.F,true,at30,error),"30 Hz smoothing succeeds");}
    std::vector<m::Mat4> fk30;
    a::global_transforms(skeleton,at30,fk30);
    expect(std::abs(fk30[3].m[3][1]-fk[3].m[3][1])<1e-5F,"equal-time smoothing agrees at 30/60 Hz");
    {
        auto far_world=p::World::create({},error);
        if(!far_world){std::cerr<<error;return 1;}
        auto far_floor=floor;far_floor.position={64000,-.5,-64000};
        const auto far_ground=far_world->create_body(far_floor,error);
        if(!far_ground){std::cerr<<error;return 1;}
        foot_placement::Controller fitted;
        expect(fitted.configure(skeleton,roles,error),"fitted far-origin controller calibrates");
        const foot_placement::Placement mapped{{64000,0,-64000},m::from_axis_angle({0,1,0},0.8F),1.5,{.1,0,.2}};
        a::Pose far_pose;
        for(int i=0;i<60;++i){expect(fitted.update(rest,mapped,*far_world,1.F/60.F,true,far_pose,error),"fitted world targets solve in model space");}
        std::vector<m::Mat4> far_fk;
        a::global_transforms(skeleton,far_pose,far_fk);
        const auto world_foot=foot_placement::to_world(mapped,foot_placement::position(far_fk[3]));
        expect(std::abs(world_foot.y-.03)<.001,"fitting/yaw/far-origin bridge reaches real world clearance");
    }
    foot_placement::Controller ankle;
    expect(ankle.configure(skeleton,roles,error,.12),"per-rig ankle clearance calibrates");
    a::Pose ankle_pose;
    for(int i=0;i<60;++i){expect(ankle.update(rest,actor,*world,1.F/60.F,true,ankle_pose,error),"custom clearance solves");}
    std::vector<m::Mat4> ankle_fk;
    a::global_transforms(skeleton,ankle_pose,ankle_fk);
    expect(std::abs(ankle_fk[3].m[3][1]-.12F)<.001F,"custom ankle clearance reaches the requested height");
    expect(!ankle.configure(skeleton,roles,error,-.1),"negative clearance refused");
    expect(controller.stats().contacts==120&&controller.stats().clamped==0,"bilateral contacts are diagnosed");
    expect(world->destroy_body(*ground),"contact fixture removed");
    expect(controller.update(rest,actor,*world,1.F/60.F,true,output,error),"lost contact succeeds safely");
    a::global_transforms(skeleton,output,fk);
    expect(std::abs(fk[3].m[3][1])<1e-6F,"lost contact clears an existing correction");
    const auto restored=world->create_body(floor,error);
    if(!restored){std::cerr<<error;return 1;}
    foot_placement::Controller teleported;
    expect(teleported.configure(skeleton,roles,error),"teleport controller calibrates");
    a::Pose before_teleport;
    for(int i=0;i<30;++i){expect(teleported.update(rest,actor,*world,1.F/60.F,true,before_teleport,error),"warm contact before teleport");}
    auto shifted=actor;shifted.feet.x=2.0;
    expect(teleported.update(rest,shifted,*world,1.F/60.F,true,before_teleport,error),"teleport onto valid ground resets");
    a::global_transforms(skeleton,before_teleport,fk);
    expect(std::abs(fk[3].m[3][1])<1e-6F,"teleport clears a nonzero old correction");
    expect(controller.update(rest,actor,*world,1.F/60.F,false,output,error),"inactive playback resets contacts");
    a::global_transforms(skeleton,output,fk);
    expect(std::abs(fk[3].m[3][1])<1e-6F,"inactive output is original animation");
    actor.feet={100,0,0};
    expect(controller.update(rest,actor,*world,1.F/60.F,true,output,error),"teleport resets safely");
    expect(controller.update(rest,actor,*world,1.F/60.F,true,output,error)&&controller.stats().misses>=2,"missing ground resets each leg");
    actor.feet={};controller.reset();
    auto broken=rest;broken[5].translation={0,-1,0};
    const auto previous=output;
    const auto before_stats=controller.stats().frames;
    expect(!controller.update(broken,actor,*world,1.F/60.F,true,output,error)&&output[1].rotation.w==previous[1].rotation.w
        &&controller.stats().frames==before_stats,"invalid bilateral pose fails without committing contact state");
    expect(controller.update(rest,actor,*world,std::numeric_limits<float>::quiet_NaN(),true,output,error),"invalid delta resets instead of poisoning state");
    auto raised=p::World::create({},error);
    floor.position.y=-.35;
    const auto step=raised->create_body(floor,error);
    expect(step.has_value(),"raised terrain fixture created");
    controller.reset();
    for(int frame=0;frame<60;++frame){expect(controller.update(rest,actor,*raised,1.F/60.F,true,output,error),"raised terrain contact solves");}
    a::global_transforms(skeleton,output,fk);
    expect(std::abs(fk[3].m[3][1]-.18F)<.001F,"raised surface changes actual foot position");
    floor.position.y=-.19;
    auto high=p::World::create({},error);const auto too_high=high->create_body(floor,error);
    expect(too_high.has_value(),"high fixture created");
    controller.reset();
    expect(controller.update(rest,actor,*high,1.F/60.F,true,output,error)&&controller.stats().contacts==240,
        "over-limit hit is not applied");
    a::global_transforms(skeleton,output,fk);
    expect(std::abs(fk[3].m[3][1])<1e-6F,"bounded rejection leaves original foot position");
    expect(controller.update(rest,actor,*world,1.F,true,output,error),"long frame delta resets safely");
    return failures==0?0:1;
}
