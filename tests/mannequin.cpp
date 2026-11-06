#include "animated_mannequin.hpp"

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
