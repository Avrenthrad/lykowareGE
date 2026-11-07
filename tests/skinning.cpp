#include <lykoware/skinning.hpp>

#include <lykoware/animation.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace math = lykoware::math;
namespace animation = lykoware::animation;
int failures = 0;

void expect(bool condition, std::string_view reason)
{
    if (!condition) {
        std::cerr << "FAIL: " << reason << '\n';
        ++failures;
    }
}

bool near(math::Vec3 a, math::Vec3 b)
{
    return math::length(a - b) < 1e-5F;
}

bool same(const lykoware::MeshData& a, const lykoware::MeshData& b)
{
    return a.positions == b.positions && a.normals == b.normals && a.indices == b.indices && a.uvs == b.uvs;
}

struct Inputs {
    lykoware::MeshData mesh{{{0.F, 0.F, 0.F}, {1.F, 0.F, 0.F}, {0.F, 1.F, 0.F}},
                            {{0.F, 0.F, 1.F}, {0.F, 0.F, 1.F}, {0.F, 0.F, 1.F}},
                            {0, 1, 2}, {{0.F, 0.F}, {1.F, 0.F}, {0.F, 1.F}}};
    std::vector<std::array<std::uint16_t, 4>> joints = std::vector<std::array<std::uint16_t, 4>>(3, {0, 1, 65535, 65535});
    std::vector<math::Vec4> weights = std::vector<math::Vec4>(3, {0.25F, 0.75F, 0.F, 0.F});
    std::vector<math::Mat4> palette = std::vector<math::Mat4>(2);

    bool run(lykoware::MeshData& out, std::string& error) const
    {
        return animation::deform_mesh(mesh, joints, weights, palette, out, error);
    }
};

void test_values()
{
    Inputs in;
    lykoware::MeshData out;
    std::string error = "previous failure";
    expect(in.run(out, error) && error.empty() && same(out, in.mesh), "identity palette preserves the bind mesh exactly");
    in.palette[0] = math::translation({4.F, 0.F, 0.F});
    in.palette[1] = math::translation({0.F, 2.F, 0.F});
    expect(in.run(out, error) && near(out.positions[1], {2.F, 1.5F, 0.F}), "four-slot weighted translations use the right joints and weights");
    expect(out.indices == in.mesh.indices && out.uvs == in.mesh.uvs && out.normals == in.mesh.normals,
           "deformation preserves topology, UVs and translation-only normals");
    const auto original = in.mesh;
    expect(animation::deform_mesh(in.mesh, in.joints, in.weights, in.palette, in.mesh, error)
               && same(in.mesh, out), "output may alias bind input");
    expect(!same(original, in.mesh), "the alias check actually deformed vertices");

    in = Inputs{};
    in.palette = {math::translation({1.F, 0.F, 0.F}), math::translation({0.F, 2.F, 0.F}),
                  math::translation({0.F, 0.F, 3.F}), math::translation({4.F, 0.F, 0.F})};
    in.joints.assign(3, {0, 1, 2, 3});
    in.weights.assign(3, {0.125F, 0.125F, 0.25F, 0.5F});
    expect(in.run(out, error) && near(out.positions[0], {2.125F, 0.25F, 0.75F}), "all four influence slots contribute");
    in = Inputs{};
    in.palette.resize(animation::max_joints);
    in.joints.assign(3, {255, 65535, 65535, 65535});
    in.weights.assign(3, {1.F, 0.F, 0.F, 0.F});
    expect(in.run(out, error) && same(out, in.mesh), "the final supported joint works while zero-weight garbage slots are ignored");

    in = Inputs{};
    in.palette[0] = math::scaling({2.F, 1.F, 0.5F});
    in.palette[1] = in.palette[0];
    const auto diagonal = math::normalize(math::Vec3{1.F, 0.F, 1.F});
    in.mesh.normals.assign(3, diagonal);
    expect(in.run(out, error) && near(out.normals[0], math::normalize(math::Vec3{0.5F, 0.F, 2.F})),
           "non-uniform scale uses inverse transpose, not the position matrix");
    in.palette[0] = math::rotation(math::from_axis_angle({0.F, 0.F, 1.F}, 1.57079632679F)) * in.palette[0];
    in.palette[1] = in.palette[0];
    expect(in.run(out, error) && near(out.normals[0], math::normalize(math::Vec3{0.F, 0.5F, 2.F}))
               && near(out.positions[1], {0.F, 2.F, 0.F}), "rotation and scale preserve column-major convention");
    in.palette.assign(2, math::scaling({-1.F, 1.F, 1.F}));
    in.mesh.normals.assign(3, math::Vec3{1.F, 0.F, 0.F});
    expect(in.run(out, error) && near(out.normals[0], {-1.F, 0.F, 0.F}), "reflection retains determinant sign for normals");

    // The inverse transpose of the blend is different from a blend of inverse transposes.
    in = Inputs{};
    in.mesh.normals.assign(3, diagonal);
    in.palette[0] = math::scaling({4.F, 1.F, 1.F});
    in.palette[1] = math::scaling({1.F, 1.F, 2.F});
    expect(in.run(out, error) && near(out.normals[0], diagonal), "the weighted matrix, not each joint separately, defines the normal transform");
    in = Inputs{};
    in.weights.assign(3, {0.25001F, 0.75001F, 0.F, 0.F});
    expect(in.run(out, error) && same(out, in.mesh), "near-unit weights are normalised without moving an identity mesh");
}

void test_failures()
{
    const lykoware::MeshData sentinel = Inputs{}.mesh;
    auto refuses = [&](const Inputs& in, std::string_view reason) {
        auto out = sentinel;
        std::string error;
        expect(!in.run(out, error) && !error.empty() && same(out, sentinel), reason);
    };
    Inputs in;
    in.mesh.indices[2] = 999;
    refuses(in, "invalid bind mesh fails without replacing output");
    in = Inputs{}; in.joints.pop_back(); refuses(in, "missing joint entries fail");
    in = Inputs{}; in.weights.pop_back(); refuses(in, "missing weight entries fail");
    in = Inputs{}; in.palette.clear(); refuses(in, "empty palette fails");
    in = Inputs{}; in.palette.resize(animation::max_joints + 1); refuses(in, "oversized palette fails");
    in = Inputs{}; in.joints.back()[1] = 2; refuses(in, "an out-of-range weighted joint fails even late in the mesh");
    in = Inputs{}; in.weights.back().z = -0.1F; refuses(in, "negative weights fail");
    in = Inputs{}; in.weights.back() = {}; refuses(in, "zero total weight fails");
    in = Inputs{}; in.weights.back().x = 1.F; refuses(in, "unnormalised weights fail");
    in = Inputs{}; in.weights.back().x = std::numeric_limits<float>::quiet_NaN(); refuses(in, "NaN weights fail");
    in = Inputs{}; in.weights.back().x = std::numeric_limits<float>::infinity(); refuses(in, "infinite weights fail");
    in = Inputs{}; in.palette[1].m[2][1] = std::numeric_limits<float>::quiet_NaN(); refuses(in, "NaN palette fails");
    in = Inputs{}; in.palette[1].m[2][1] = std::numeric_limits<float>::infinity(); refuses(in, "infinite palette fails");
    in = Inputs{}; in.palette[1].m[0][3] = 0.1F; refuses(in, "projective palettes fail");
    in = Inputs{}; in.palette[1].m[3][3] = 0.F; refuses(in, "invalid affine last row fails");
    in = Inputs{}; in.palette.assign(2, math::scaling({1.F, 0.F, 1.F})); refuses(in, "singular matrices fail");
    in = Inputs{}; in.weights.assign(3, {0.5F, 0.5F, 0.F, 0.F});
    in.palette[1] = math::scaling({-1.F, 1.F, 1.F}); refuses(in, "individually invertible matrices can blend to a singular matrix");
    in = Inputs{}; in.mesh.positions[1].x = std::numeric_limits<float>::max();
    in.palette.assign(2, math::scaling({2.F, 1.F, 1.F})); refuses(in, "finite inputs that overflow output float range fail");
    in = Inputs{}; in.joints.back()[1] = 2;
    const auto before = in.mesh;
    std::string error;
    expect(!animation::deform_mesh(in.mesh, in.joints, in.weights, in.palette, in.mesh, error) && same(before, in.mesh),
           "failed aliasing deformation leaves the bind mesh intact");
}

}

int main()
{
    test_values();
    test_failures();
    return failures == 0 ? 0 : 1;
}
