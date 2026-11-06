#include "animated_mannequin.hpp"
#include "character_fit.hpp"
#include "rig_roles.hpp"

#include <lykoware/skinning.hpp>

#include <cmath>
#include <iostream>
#include <limits>
#include <string>

int main()
{
    namespace animation = lykoware::animation;
    namespace math = lykoware::math;
    int failures = 0;
    auto expect = [&](bool ok, const char* why) {
        if (!ok) { std::cerr << "FAIL: " << why << '\n'; ++failures; }
    };
    character_fit::Placement fit;
    std::string fit_error;
    const lykoware::Bounds authored{{10.F, -4.F, 20.F}, {14.F, 4.F, 26.F}};
    expect(character_fit::fit(authored, 2.F, fit, fit_error) && fit.scale == 0.25F,
           "authored height fits with uniform scale");
    const auto feet = math::Vec3{12.F,-4.F,23.F} * fit.scale + fit.offset;
    const auto head = math::Vec3{12.F,4.F,23.F} * fit.scale + fit.offset;
    expect(math::length(feet) < 1e-6F && math::length(head - math::Vec3{0.F,2.F,0.F}) < 1e-6F,
           "off-centre bind feet and head align to controller origin and target height");
    const auto before_fit = fit;
    expect(!character_fit::fit({}, 2.F, fit, fit_error) && fit.scale == before_fit.scale && fit.offset == before_fit.offset,
           "zero-height fitting fails transactionally");
    expect(!character_fit::fit(authored, std::numeric_limits<float>::quiet_NaN(), fit, fit_error), "nonfinite height refused");
    lykoware::animation::Skeleton rig;
    for (const auto* name : rig_roles::names) {
        lykoware::animation::Joint joint{}; joint.name = name; rig.joints.push_back(joint);
    }
    rig.joints[1].parent=0;rig.joints[2].parent=1;rig.joints[3].parent=2;
    rig.joints[4].parent=0;rig.joints[5].parent=4;rig.joints[6].parent=5;
    rig_roles::Names names;
    for (std::size_t i=0;i<names.size();++i) { names[i]=rig_roles::names[i]; }
    rig_roles::Indices roles{};
    std::string rig_error;
    expect(rig_roles::resolve(rig,names,roles,rig_error) && roles[6]==6, "explicit leg roles resolve by name");
    const auto before_roles=roles;
    auto wrong=names;wrong[1]="missing";
    expect(!rig_roles::resolve(rig,wrong,roles,rig_error) && roles==before_roles, "missing role fails transactionally");
    wrong=names;wrong[2]=names[1];
    expect(!rig_roles::resolve(rig,wrong,roles,rig_error), "aliased roles refused");
    rig.joints[3].parent=0;
    expect(!rig_roles::resolve(rig,names,roles,rig_error), "incorrect leg ancestry refused");
    rig.joints[3].parent=2;rig.joints[6].name=rig.joints[5].name;
    expect(!rig_roles::resolve(rig,names,roles,rig_error), "ambiguous names refused");
    const auto asset = training_mannequin::make();
    std::string error;
    expect(lykoware::valid_mesh(asset.mesh, error) && animation::valid_skeleton(asset.skeleton, error)
        && animation::valid_clip(asset.idle, asset.skeleton, error) && animation::valid_clip(asset.walk, asset.skeleton, error),
        "procedural asset and both clips are valid");
    expect(asset.parts.size() == 6 && asset.skeleton.joints.size() == 7, "six original body parts use seven joints");
    std::vector<math::Mat4> matrices;
    animation::skinning_matrices(asset.skeleton, animation::rest_pose(asset.skeleton), matrices);
    lykoware::MeshData rest;
    expect(animation::deform_mesh(asset.mesh, asset.joints, asset.weights, matrices, rest, error), "the rig deforms at rest");
    if (rest.positions.size() == asset.mesh.positions.size()) {
        for (std::size_t i = 0; i < rest.positions.size(); ++i) {
            expect(math::length(rest.positions[i] - asset.mesh.positions[i]) < 1e-5F, "inverse binds preserve rest geometry");
        }
    }
    expect(std::abs(lykoware::bounds(asset.mesh).min.y) < 1e-5F && lykoware::bounds(asset.mesh).max.y < 1.8F,
           "bind mannequin stands on its feet below the default capsule height");
    training_mannequin::Playback idle, walking;
    for (int i = 0; i < 60; ++i) {
        idle.advance(asset, 1.F / 60.F, 0.F);
        walking.advance(asset, 1.F / 60.F, 4.F);
    }
    expect(idle.walk_weight == 0.F && walking.walk_weight > 0.99F, "speed smoothly selects idle and walk");
    expect(idle.blended[1].translation.y > 0.965F && idle.blended[5].rotation.x == 0.F,
           "idle breathes without swinging the legs");
    expect(walking.blended[5].rotation.x * walking.blended[6].rotation.x < 0.F,
           "the walk swings the two legs in opposite directions");
    lykoware::MeshData walking_mesh;
    expect(animation::deform_mesh(asset.mesh, asset.joints, asset.weights, walking.matrices, walking_mesh, error),
           "a sampled walk produces a valid deformed mesh");
    float largest_move = 0.F;
    for (std::size_t i = 0; i < walking_mesh.positions.size(); ++i) {
        largest_move = std::max(largest_move, math::length(walking_mesh.positions[i] - asset.mesh.positions[i]));
    }
    expect(largest_move > 0.2F, "walking visibly moves the limbs");
    for (int i = 0; i < 60; ++i) { walking.advance(asset, 1.F / 60.F, 0.F); }
    expect(walking.walk_weight < 0.001F, "stopping blends back to idle");
    training_mannequin::Playback at30, at144;
    for (int i = 0; i < 30; ++i) { at30.advance(asset, 1.F / 30.F, 4.F); }
    for (int i = 0; i < 144; ++i) { at144.advance(asset, 1.F / 144.F, 4.F); }
    expect(std::abs(at30.walk_weight - at144.walk_weight) < 1e-5F, "blend response is stable across frame rates");
    // Loop endpoints are equivalent; compare poses, not wrapped clock values.
    expect(std::abs(at30.blended[5].rotation.x - at144.blended[5].rotation.x) < 1e-4F,
           "equal elapsed time samples the same walk across frame rates");
    const auto before = idle.idle_seconds;
    idle.advance(asset, -1.F, 0.F);
    idle.advance(asset, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity());
    expect(idle.idle_seconds == before && idle.walk_weight == 0.F, "bad drawing time/speed does not poison playback");
    return failures == 0 ? 0 : 1;
}
