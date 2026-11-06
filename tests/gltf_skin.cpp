// assets.gltf_skin: reading skinned, animated glTF 2.0 models (ADR 0050). Every model is built
// in memory, so the repository holds no model files.

#include <lykoware/animation.hpp>
#include <lykoware/gltf.hpp>

#include "support/gltf_builder.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace anim = lykoware::animation;
namespace gltf = lykoware::gltf;
namespace math = lykoware::math;
using gltf_test::Model;
using math::Vec3;

constexpr float pi = std::numbers::pi_v<float>;

int g_failures = 0;

void expect(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

bool near(Vec3 a, Vec3 b, float tolerance = 1e-5F)
{
    return std::abs(a.x - b.x) <= tolerance && std::abs(a.y - b.y) <= tolerance && std::abs(a.z - b.z) <= tolerance;
}

bool same_rotation(math::Quat a, math::Quat b, float tolerance = 1e-4F)
{
    const Vec3 probes[] = {{1.F, 0.F, 0.F}, {0.F, 1.F, 0.F}, {0.F, 0.F, 1.F}};
    for (const Vec3 p : probes) {
        if (!near(math::rotate(a, p), math::rotate(b, p), tolerance)) {
            return false;
        }
    }
    return true;
}

std::string text(Vec3 v)
{
    return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")";
}

std::string number(std::size_t n)
{
    return std::to_string(n);
}

// An accessor with the normalized flag set.
std::size_t normalised(Model& m, std::size_t view, std::uint32_t component, std::size_t count, const char* type)
{
    m.accessors.push_back("{\"bufferView\":" + number(view) + ",\"componentType\":" + std::to_string(component)
                          + ",\"count\":" + number(count) + ",\"type\":\"" + type + "\",\"normalized\":true}");
    return m.accessors.size() - 1;
}

std::size_t floats(Model& m, const std::vector<float>& values, const char* type, std::size_t width)
{
    return m.accessor(m.view(values), 5126, values.size() / width, type);
}

std::vector<float> matrix(const math::Mat4& a)
{
    std::vector<float> out;
    for (const auto& column : a.m) {
        out.insert(out.end(), std::begin(column), std::end(column));
    }
    return out;
}

std::int16_t snorm16(float value)
{
    return static_cast<std::int16_t>(std::lround(value * 32767.F));
}

// The parts of the arm model a test may change before it is written out.
struct Arm {
    Model m;
    std::string armature = R"("translation":[0,5,0])";
    std::string mesh_node = R"({"name":"skin mesh","mesh":0,"skin":0,"translation":[100,0,0]})";
    std::string skin_joints = "[1,2]";
    std::string extra_nodes;
    std::string extra_animation_channels;
    std::string attributes;
    std::size_t inverse_binds = 0;
    std::size_t wave_times = 0;
    std::size_t wave_rotations = 0;
    std::size_t elbow_times = 0;
    std::size_t elbow_steps = 0;
    std::size_t cubic_times = 0;
    std::size_t cubic_values = 0;
    std::size_t prop_moves = 0;
    std::string wave_interpolation = "LINEAR";
    std::string wave_path = "rotation";

    std::vector<std::byte> file(bool glb = false)
    {
        m.nodes = "[{\"name\":\"Armature\"," + armature + ",\"children\":[1,3]},"
                  R"({"name":"shoulder","children":[2]},)"
                  R"({"name":"elbow","translation":[1,0,0]},)"
            + mesh_node + extra_nodes + "]";
        m.scenes = "[{\"nodes\":[0]}]";
        m.extra = ",\"skins\":[{\"joints\":" + skin_joints + ",\"inverseBindMatrices\":" + number(inverse_binds)
            + "}],\"animations\":[{\"name\":\"wave\",\"samplers\":["
              "{\"input\":" + number(wave_times) + ",\"output\":" + number(wave_rotations) + ",\"interpolation\":\""
            + wave_interpolation + "\"},"
              "{\"input\":" + number(wave_times) + ",\"output\":" + number(prop_moves) + "}],"
              "\"channels\":[{\"sampler\":0,\"target\":{\"node\":1,\"path\":\"" + wave_path + "\"}},"
              "{\"sampler\":1,\"target\":{\"node\":3,\"path\":\"translation\"}},"
              "{\"sampler\":1,\"target\":{\"node\":1,\"path\":\"weights\"}}" + extra_animation_channels + "]},"
              "{\"samplers\":[{\"input\":" + number(elbow_times) + ",\"output\":" + number(elbow_steps)
            + ",\"interpolation\":\"STEP\"},{\"input\":" + number(cubic_times) + ",\"output\":" + number(cubic_values)
            + ",\"interpolation\":\"CUBICSPLINE\"}],"
              "\"channels\":[{\"sampler\":0,\"target\":{\"node\":2,\"path\":\"translation\"}},"
              "{\"sampler\":1,\"target\":{\"node\":2,\"path\":\"scale\"}}]}]";
        m.meshes = "[{\"primitives\":[{\"attributes\":{" + attributes + "},\"indices\":" + number(indices) + "}]}]";
        return glb ? m.glb() : m.gltf();
    }

    std::size_t indices = 0;
};

// An arm: an "Armature" node 5 m up holds a shoulder joint and, 1 m along +X from it, an elbow
// joint. Three vertices, in bind space: one on the upper arm (shoulder), one on the forearm
// (elbow), one at the elbow shared half and half (weights as normalised bytes). The mesh node's
// own translation must be ignored. Animations: "wave" turns the shoulder a quarter turn about Z
// over a second (keys as normalised 16-bit integers), and also keys the mesh node (not in the
// skeleton) and morph weights, both left out; the unnamed second one steps the elbow and scales
// it on a cubic spline.
Arm arm()
{
    Arm a;
    Model& m = a.m;
    const std::size_t p = m.vec3s({{0.5F, 5.F, 0.F}, {1.5F, 5.F, 0.F}, {1.F, 5.F, 0.F}});
    const std::size_t n = m.vec3s(std::vector<Vec3>(3, Vec3{0.F, 0.F, 1.F}));
    const std::size_t j = m.accessor(m.view(std::vector<std::uint8_t>{0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0}), 5121, 3, "VEC4");
    const std::size_t w =
        normalised(m, m.view(std::vector<std::uint8_t>{255, 0, 0, 0, 255, 0, 0, 0, 128, 128, 0, 0}), 5121, 3, "VEC4");
    a.attributes = "\"POSITION\":" + number(p) + ",\"NORMAL\":" + number(n) + ",\"JOINTS_0\":" + number(j)
        + ",\"WEIGHTS_0\":" + number(w);
    a.indices = m.indices(std::vector<std::uint16_t>{0, 1, 2});
    std::vector<float> binds = matrix(math::translation({0.F, -5.F, 0.F}));
    const std::vector<float> elbow_bind = matrix(math::translation({-1.F, -5.F, 0.F}));
    binds.insert(binds.end(), elbow_bind.begin(), elbow_bind.end());
    a.inverse_binds = floats(m, binds, "MAT4", 16);
    a.wave_times = floats(m, {0.F, 1.F}, "SCALAR", 1);
    const math::Quat turned = math::from_axis_angle({0.F, 0.F, 1.F}, pi / 2.F);
    a.wave_rotations = normalised(m,
                                  m.view(std::vector<std::int16_t>{0, 0, 0, 32767, snorm16(turned.x), snorm16(turned.y),
                                                                   snorm16(turned.z), snorm16(turned.w)}),
                                  5122, 2, "VEC4");
    a.prop_moves = floats(m, {0.F, 0.F, 0.F, 3.F, 0.F, 0.F}, "VEC3", 3);
    a.elbow_times = floats(m, {0.F, 0.5F}, "SCALAR", 1);
    a.elbow_steps = floats(m, {1.F, 0.F, 0.F, 2.F, 0.F, 0.F}, "VEC3", 3);
    a.cubic_times = floats(m, {0.F, 2.F}, "SCALAR", 1);
    a.cubic_values = floats(m, {0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 3, 3, 3, 0, 0, 0}, "VEC3", 3);
    return a;
}

std::optional<gltf::SkinnedModel> read(const std::vector<std::byte>& file, std::string& error)
{
    return gltf::read_skinned(file, {}, error);
}

const anim::Clip* clip_named(const gltf::SkinnedModel& model, std::string_view name)
{
    for (const anim::Clip& clip : model.clips) {
        if (clip.name == name) {
            return &clip;
        }
    }
    return nullptr;
}

void test_arm()
{
    Arm a = arm();
    std::string error;
    const auto model = read(a.file(), error);
    expect(model.has_value(), "the skinned arm is read: " + error);
    if (!model) {
        return;
    }
    const auto& mesh = model->model.mesh;
    expect(mesh.positions.size() == 3 && model->joints.size() == 3 && model->weights.size() == 3,
           "every vertex has joints and weights");
    expect(near(mesh.positions[1], {1.5F, 5.F, 0.F}), "the mesh is in bind space: the skinned mesh node's own translation is ignored");

    const anim::Skeleton& skeleton = model->skeleton;
    expect(skeleton.joints.size() == 3, "the skeleton is the skin's two joints and the armature above them");
    if (skeleton.joints.size() == 3) {
        expect(skeleton.joints[0].name == "Armature" && !skeleton.joints[0].parent, "the armature node comes first, as the root");
        expect(skeleton.joints[1].name == "shoulder" && skeleton.joints[1].parent == 0, "then the shoulder under it");
        expect(skeleton.joints[2].name == "elbow" && skeleton.joints[2].parent == 1, "then the elbow under the shoulder");
        expect(near(skeleton.joints[0].rest.translation, {0.F, 5.F, 0.F}) && near(skeleton.joints[2].rest.translation, {1.F, 0.F, 0.F}),
               "rest poses come from the nodes");
        expect(math::transform_point(skeleton.joints[0].inverse_bind, {1.F, 2.F, 3.F}) == Vec3{1.F, 2.F, 3.F},
               "the node above the joints has no inverse bind matrix of its own");
        expect(near(math::transform_point(skeleton.joints[2].inverse_bind, {}), {-1.F, -5.F, 0.F}),
               "the joints get the skin's inverse bind matrices");
    }
    expect(model->joints[0][0] == 1 && model->joints[1][0] == 2, "vertex joints are mapped from the skin's list to the skeleton");
    expect(model->joints[2][0] == 1 && model->joints[2][1] == 2 && std::abs(model->weights[2].x - 0.5F) < 1e-6F
               && std::abs(model->weights[2].y - 0.5F) < 1e-6F,
           "normalised byte weights 128 and 128 become exactly a half each");
    expect(model->weights[0].x == 1.F && model->weights[0].y == 0.F, "a single joint has the whole weight");

    std::vector<math::Mat4> matrices;
    anim::Pose pose = anim::rest_pose(skeleton);
    anim::skinning_matrices(skeleton, pose, matrices);
    bool at_rest = true;
    for (std::size_t v = 0; v < 3; ++v) {
        at_rest = at_rest && near(anim::skin_point(matrices, model->joints[v], model->weights[v], mesh.positions[v]), mesh.positions[v]);
    }
    expect(at_rest, "at rest the skinned mesh stays where it was modelled");

    expect(model->clips.size() == 2, "both animations are read");
    const anim::Clip* wave = clip_named(*model, "wave");
    expect(wave != nullptr && wave->channels.size() == 1,
           "\"wave\" keeps only its skeleton channel: the mesh node's and the morph weights' are left out");
    if (wave != nullptr) {
        std::string clip_error;
        expect(anim::valid_clip(*wave, skeleton, clip_error), "and samples on the skeleton: " + clip_error);
        anim::sample(*wave, 1.F, false, pose);
        expect(same_rotation(pose[1].rotation, math::from_axis_angle({0.F, 0.F, 1.F}, pi / 2.F)),
               "rotation keys stored as normalised 16-bit integers are read");
        anim::skinning_matrices(skeleton, pose, matrices);
        const Vec3 forearm = anim::skin_point(matrices, model->joints[1], model->weights[1], mesh.positions[1]);
        expect(near(forearm, {0.F, 6.5F, 0.F}, 1e-3F),
               "a quarter turn of the shoulder swings the forearm vertex up to (0, 6.5, 0): " + text(forearm));
    }
    const anim::Clip* second = clip_named(*model, "animation 1");
    expect(second != nullptr && second->channels.size() == 2, "an unnamed animation is named by its index");
    if (second != nullptr) {
        pose = anim::rest_pose(skeleton);
        anim::sample(*second, 0.75F, false, pose);
        expect(near(pose[2].translation, {2.F, 0.F, 0.F}), "a STEP sampler is read as step");
        anim::sample(*second, 1.F, false, pose);
        expect(near(pose[2].scale, {2.F, 2.F, 2.F}), "a CUBICSPLINE sampler is read with its tangents: halfway from 1 to 3 is 2");
    }

    std::string glb_error;
    const auto from_glb = read(a.file(true), glb_error);
    expect(from_glb && from_glb->model.mesh.positions == mesh.positions && from_glb->joints == model->joints,
           "the same model as .glb reads the same: " + glb_error);

    const auto rigid = gltf::read_model(a.file(), {}, error);
    expect(rigid && near(rigid->mesh.positions[1], {101.5F, 10.F, 0.F}),
           "read_model still ignores the skin and places the mesh by its nodes, as before");
}

void test_node_matrices()
{
    // The armature as a matrix: decomposed back into translation, rotation and scale.
    Arm a = arm();
    const math::Quat turn = math::from_axis_angle({0.F, 1.F, 0.F}, pi / 6.F);
    const math::Mat4 placed = math::trs({0.F, 5.F, 0.F}, turn, {2.F, 2.F, 2.F});
    std::string values;
    for (const float v : matrix(placed)) {
        values += (values.empty() ? "" : ",") + std::to_string(v);
    }
    a.armature = "\"matrix\":[" + values + "]";
    std::string error;
    auto model = read(a.file(), error);
    expect(model.has_value(), "a skeleton node given as a matrix is read: " + error);
    if (model) {
        const anim::JointPose& rest = model->skeleton.joints[0].rest;
        expect(near(rest.translation, {0.F, 5.F, 0.F}) && same_rotation(rest.rotation, turn) && near(rest.scale, {2.F, 2.F, 2.F}, 1e-4F),
               "its matrix is split into translation, rotation and scale");
    }
    a.armature = R"("matrix":[-1,0,0,0, 0,1,0,0, 0,0,1,0, 0,5,0,1])";
    model = read(a.file(), error);
    expect(model && near(model->skeleton.joints[0].rest.scale, {-1.F, 1.F, 1.F}) && same_rotation(model->skeleton.joints[0].rest.rotation, {}),
           "a mirroring matrix becomes a negative scale: " + error);
    a.armature = R"("matrix":[1,0,0,0, 1,1,0,0, 0,0,1,0, 0,5,0,1])";
    expect(!read(a.file(), error) && error.find("shear") != std::string::npos, "a sheared matrix is refused: " + error);
}

void test_flat_normals()
{
    // Without normals every triangle gets its own corners; their joints and weights go with them.
    Arm a = arm();
    a.attributes = a.attributes.substr(0, a.attributes.find(",\"NORMAL\"")) + a.attributes.substr(a.attributes.find(",\"JOINTS_0\""));
    std::string error;
    const auto model = read(a.file(), error);
    expect(model && model->joints.size() == model->model.mesh.positions.size() && model->weights.size() == model->joints.size(),
           "flat-shaded skinned vertices keep their joints and weights: " + error);
}

void refused(Arm a, std::string_view what, std::string_view reason)
{
    std::string error;
    const auto model = read(a.file(), error);
    expect(!model && error.find(reason) != std::string::npos, std::string{what} + ": " + error);
}

void test_refusals()
{
    std::string error;
    Model plain;
    const std::size_t p = plain.vec3s({{0.F, 0.F, 0.F}, {1.F, 0.F, 0.F}, {0.F, 1.F, 0.F}});
    plain.meshes = "[{\"primitives\":[{\"attributes\":{\"POSITION\":" + number(p) + "}}]}]";
    expect(!read(plain.gltf(), error) && error.find("no skinned mesh") != std::string::npos,
           "a model without a skin is refused by read_skinned: " + error);

    Arm unskinned = arm();
    unskinned.mesh_node = R"({"mesh":0})";
    refused(unskinned, "a scene whose only mesh has no skin is refused", "no skinned mesh");
    Arm extra = arm();
    extra.extra_nodes = R"(,{"mesh":0})";
    extra.armature = R"("translation":[0,5,0])";
    {
        std::string e;
        extra.file();
        extra.m.nodes.replace(extra.m.nodes.find("\"children\":[1,3]"), 16, "\"children\":[1,3,4]");
        const auto model = read(extra.m.gltf(), e);
        expect(!model && e.find("without the model's skin") != std::string::npos,
               "a second, unskinned mesh in the scene is refused: " + e);
    }
    Arm two_skins = arm();
    two_skins.extra_nodes = R"(,{"mesh":0,"skin":1})";
    {
        std::string e;
        two_skins.file();
        two_skins.m.extra.replace(two_skins.m.extra.find("}],\"animations\""), 3, "},{\"joints\":[2]}],");
        const auto model = read(two_skins.m.gltf(), e);
        expect(!model && e.find("more than one skin") != std::string::npos, "meshes with two skins are refused: " + e);
    }
    Arm no_weights = arm();
    no_weights.attributes = no_weights.attributes.substr(0, no_weights.attributes.find(",\"WEIGHTS_0\""));
    refused(no_weights, "a skinned primitive without weights is refused", "JOINTS_0 and WEIGHTS_0");
    Arm eight = arm();
    eight.attributes += ",\"JOINTS_1\":0";
    refused(eight, "more than four joints a vertex is refused", "JOINTS_1");
    Arm outside = arm();
    outside.attributes = outside.attributes.substr(0, outside.attributes.find(",\"JOINTS_0\""))
        + ",\"JOINTS_0\":" + number(outside.m.accessor(outside.m.view(std::vector<std::uint8_t>{0, 0, 0, 0, 5, 0, 0, 0, 0, 1, 0, 0}), 5121, 3, "VEC4"))
        + outside.attributes.substr(outside.attributes.find(",\"WEIGHTS_0\""));
    refused(outside, "a joint index outside the skin is refused", "outside the skin");
    Arm float_joints = arm();
    float_joints.attributes = float_joints.attributes.substr(0, float_joints.attributes.find(",\"JOINTS_0\""))
        + ",\"JOINTS_0\":" + number(floats(float_joints.m, std::vector<float>(12, 0.F), "VEC4", 4))
        + float_joints.attributes.substr(float_joints.attributes.find(",\"WEIGHTS_0\""));
    refused(float_joints, "float joint indices are refused", "unsigned 8- or 16-bit");
    Arm zero = arm();
    zero.attributes = zero.attributes.substr(0, zero.attributes.find(",\"WEIGHTS_0\""))
        + ",\"WEIGHTS_0\":" + number(floats(zero.m, {1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0}, "VEC4", 4));
    refused(zero, "a vertex whose weights sum to 0 is refused", "sum to 0");
    Arm negative = arm();
    negative.attributes = negative.attributes.substr(0, negative.attributes.find(",\"WEIGHTS_0\""))
        + ",\"WEIGHTS_0\":" + number(floats(negative.m, {1, 0, 0, 0, 1.5F, -0.5F, 0, 0, 1, 0, 0, 0}, "VEC4", 4));
    refused(negative, "a negative weight is refused", "negative weight");
    Arm few_binds = arm();
    few_binds.inverse_binds = floats(few_binds.m, matrix(math::Mat4{}), "MAT4", 16);
    refused(few_binds, "fewer inverse bind matrices than joints are refused", "fewer inverse bind matrices");
    Arm twice = arm();
    twice.skin_joints = "[1,1]";
    refused(twice, "a joint listed twice is refused", "a joint twice");
    Arm cycle = arm();
    cycle.extra_nodes = R"(,{"name":"loop a","children":[5]},{"name":"loop b","children":[4]})";
    cycle.skin_joints = "[1,2,4]";
    {
        std::string e;
        cycle.inverse_binds = floats(cycle.m, std::vector<float>(48, 0.F), "MAT4", 16);
        const auto model = read(cycle.file(), e);
        expect(!model && e.find("cycle") != std::string::npos, "joints in a cycle of nodes are refused: " + e);
    }
    Arm unknown_mode = arm();
    unknown_mode.wave_interpolation = "SMOOTH";
    refused(unknown_mode, "an unknown interpolation is refused", "unknown interpolation");
    Arm unknown_path = arm();
    unknown_path.wave_path = "colour";
    refused(unknown_path, "an unknown animation path is refused", "unknown target path");
    Arm backwards = arm();
    backwards.wave_times = floats(backwards.m, {1.F, 0.F}, "SCALAR", 1);
    refused(backwards, "key times going backwards are refused", "strictly increasing");
    Arm short_output = arm();
    short_output.wave_rotations = floats(short_output.m, {0, 0, 0, 1}, "VEC4", 4);
    refused(short_output, "too few keyed values are refused", "values for");
    Arm doubled = arm();
    doubled.extra_animation_channels = R"(,{"sampler":0,"target":{"node":1,"path":"rotation"}})";
    refused(doubled, "a joint's rotation keyed twice in one animation is refused", "keyed twice");

    // 257 joints under the armature: with it, 258 skeleton joints, over the limit of 256.
    Arm big = arm();
    std::string joints = "[1,2";
    for (std::size_t i = 0; i < 255; ++i) {
        big.extra_nodes += R"(,{"name":"extra"})";
        joints += "," + number(4 + i);
    }
    big.skin_joints = joints + "]";
    big.inverse_binds = floats(big.m, std::vector<float>(257 * 16, 0.F), "MAT4", 16);
    {
        std::string e;
        big.file();
        std::string children = "[1,3";
        for (std::size_t i = 0; i < 255; ++i) {
            children += "," + number(4 + i);
        }
        big.m.nodes.replace(big.m.nodes.find("\"children\":[1,3]"), 16, "\"children\":" + children + "]");
        const auto model = read(big.m.gltf(), e);
        expect(!model && e.find("more than 256") != std::string::npos, "a skeleton of more than 256 joints is refused: " + e);
    }
}

}

int main()
{
    test_arm();
    test_node_matrices();
    test_flat_normals();
    test_refusals();
    if (g_failures != 0) {
        std::cerr << g_failures << " skinned glTF assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "skinned glTF passed\n";
    return EXIT_SUCCESS;
}
