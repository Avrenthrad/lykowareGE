// math.types: Lykoware's maths module (ADR 0024). Links only lykoware::engine.

#include <lykoware/math.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <numbers>
#include <string_view>

namespace {

namespace math = lykoware::math;

int g_failures = 0;

void expect(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

bool near(float a, float b, float tolerance = 1e-5F)
{
    return std::fabs(a - b) <= tolerance;
}

bool near(math::Vec3 a, math::Vec3 b, float tolerance = 1e-5F)
{
    return near(a.x, b.x, tolerance) && near(a.y, b.y, tolerance) && near(a.z, b.z, tolerance);
}

bool near(const math::Mat4& a, const math::Mat4& b, float tolerance = 1e-5F)
{
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            if (!near(a.m[c][r], b.m[c][r], tolerance)) {
                return false;
            }
        }
    }
    return true;
}

constexpr float pi = std::numbers::pi_v<float>;

void test_vectors()
{
    const math::Vec3 x{1.F, 0.F, 0.F};
    const math::Vec3 y{0.F, 1.F, 0.F};
    const math::Vec3 z{0.F, 0.F, 1.F};
    expect(math::cross(x, y) == z && math::cross(y, z) == x && math::cross(z, x) == y,
           "cross products follow the right-hand rule");
    expect(math::dot(math::Vec3{1.F, 2.F, 3.F}, math::Vec3{4.F, 5.F, 6.F}) == 32.F, "dot product");
    expect(math::length(math::Vec3{3.F, 4.F, 0.F}) == 5.F, "length");
    expect(near(math::normalize(math::Vec3{0.F, 0.F, 7.F}), z), "normalize");
    expect(math::normalize(math::Vec3{}) == math::Vec3{}, "normalizing zero gives zero, not NaN");
    expect(math::lerp(x, y, 0.5F) == math::Vec3{0.5F, 0.5F, 0.F}, "lerp");
    expect((x + y - z) * 2.F == math::Vec3{2.F, 2.F, -2.F} && -x == math::Vec3{-1.F, 0.F, 0.F}, "arithmetic");

    // Camera-relative offsets stay exact far from the origin.
    // 0.4 mm and 0.3 mm offsets 16 km out: smaller than the 0.98 mm gap between floats there.
    const math::DVec3 far{16000.0004, 3.5, -16000.0003};
    const math::DVec3 camera{16000.0, 2.0, -16000.0};
    expect(near(math::relative_to(far, camera), {0.0004F, 1.5F, -0.0003F}, 1e-7F),
           "relative_to keeps sub-millimetre offsets 16 km out, where float positions could not");
    expect(math::DVec3{16000.25, 3.5, -1.0} + math::Vec3{0.75F, 0.F, 0.5F} == math::DVec3{16001.0, 3.5, -0.5},
           "DVec3 plus Vec3");
}

void test_quaternions()
{
    const math::Quat quarter = math::from_axis_angle({0.F, 1.F, 0.F}, pi / 2.F);
    expect(near(math::rotate(quarter, {1.F, 0.F, 0.F}), {0.F, 0.F, -1.F}),
           "a quarter turn about +Y takes +X to -Z (right-handed)");
    expect(near(math::rotate(quarter * quarter, {1.F, 0.F, 0.F}), {-1.F, 0.F, 0.F}), "two quarter turns make a half turn");
    const math::Quat about_x = math::from_axis_angle({1.F, 0.F, 0.F}, pi / 2.F);
    // b first, then a: rotate +Y about X to +Z, then about Y to +X.
    expect(near(math::rotate(quarter * about_x, {0.F, 1.F, 0.F}), {1.F, 0.F, 0.F}), "a * b applies b first");
    const math::Quat a = math::normalize(math::Quat{0.3F, -0.2F, 0.5F, 0.8F});
    const math::Quat b = math::normalize(math::Quat{-0.6F, 0.4F, 0.1F, 0.7F});
    const math::Vec3 v{0.3F, -0.9F, 1.7F};
    expect(near(math::rotate(a * b, v), math::rotate(a, math::rotate(b, v))),
           "for general rotations, rotating by a * b is rotating by b, then a");
    expect(near(math::rotate(math::conjugate(quarter), math::rotate(quarter, {0.3F, 0.4F, 0.5F})), {0.3F, 0.4F, 0.5F}),
           "the conjugate undoes a rotation");
    const math::Quat none = math::from_axis_angle({}, 1.F);
    expect(none.w == 1.F && none.x == 0.F, "a zero axis gives no rotation");
    const math::Quat n = math::normalize(math::Quat{0.F, 0.F, 0.F, 2.F});
    expect(n.w == 1.F && math::normalize(math::Quat{0.F, 0.F, 0.F, 0.F}).w == 1.F,
           "normalize scales to unit length and turns zero into no rotation");
}

void test_matrices()
{
    const math::Quat q = math::normalize(math::Quat{0.2F, -0.4F, 0.1F, 0.9F});
    const math::Vec3 p{0.7F, -1.3F, 2.2F};
    expect(near(math::transform_point(math::rotation(q), p), math::rotate(q, p)),
           "the rotation matrix agrees with rotating by the quaternion");

    const math::Mat4 model = math::trs({1.F, 2.F, 3.F}, q, {2.F, 2.F, 2.F});
    expect(near(math::transform_point(model, {0.F, 0.F, 0.F}), {1.F, 2.F, 3.F}), "trs moves the origin to t");
    expect(near(math::transform_point(model, p), math::rotate(q, p * 2.F) + math::Vec3{1.F, 2.F, 3.F}),
           "trs scales, then rotates, then translates");

    const math::Mat4 a = math::trs({1.F, 0.F, -2.F}, q, {1.F, 1.F, 1.F});
    const math::Mat4 b = math::trs({0.F, 3.F, 0.F}, math::from_axis_angle({1.F, 1.F, 0.F}, 0.7F), {1.F, 1.F, 1.F});
    const math::Mat4 c = math::translation({0.5F, 0.5F, 0.5F});
    expect(near((a * b) * c, a * (b * c)), "matrix multiplication is associative");
    expect(near(math::rigid_inverse(a) * a, math::Mat4{}), "rigid_inverse undoes a rigid transform");
    expect(near(math::Mat4{} * a, a), "the default matrix is the identity");
}

void test_camera()
{
    // A camera at (0, 2, 5) looking at the origin.
    const math::Mat4 view = math::look_at({0.F, 2.F, 5.F}, {0.F, 0.F, 0.F}, {0.F, 1.F, 0.F});
    expect(near(math::transform_point(view, {0.F, 2.F, 5.F}), {0.F, 0.F, 0.F}), "the eye maps to the view origin");
    const math::Vec3 target = math::transform_point(view, {0.F, 0.F, 0.F});
    expect(near(target.x, 0.F) && near(target.y, 0.F) && target.z < 0.F, "the target is straight ahead, down -Z");
    expect(math::transform_point(view, {1.F, 0.F, 0.F}).x > 0.F, "world +X is to the right");
    expect(math::transform_point(view, {0.F, 3.F, 0.F}).y > 0.F, "world up is up");
    expect(near(math::look_at({0.F, 0.F, 0.F}, {0.F, 0.F, 0.F}, {0.F, 1.F, 0.F}), math::Mat4{}),
           "looking at your own position gives the identity");
    expect(near(math::look_at({0.F, 0.F, 0.F}, {0.F, 5.F, 0.F}, {0.F, 1.F, 0.F}), math::Mat4{}),
           "looking straight along up gives the identity");

    const math::Mat4 projection = math::perspective(pi / 2.F, 16.F / 9.F, 0.1F);
    auto ndc = [&](math::Vec3 v) {
        const math::Vec4 clip = projection * math::Vec4{v.x, v.y, v.z, 1.F};
        return math::Vec3{clip.x / clip.w, clip.y / clip.w, clip.z / clip.w};
    };
    expect(near(ndc({0.F, 0.F, -0.1F}).z, 1.F), "the near plane has depth 1 (reversed depth)");
    const float d10 = ndc({0.F, 0.F, -10.F}).z;
    const float d16k = ndc({0.F, 0.F, -16000.F}).z;
    expect(d10 > d16k && d16k > 0.F, "depth falls with distance and stays above 0 even 16 km away");
    expect(near(ndc({0.F, 1.F, -1.F}).y, -1.F), "view-space up is the top of the image (Vulkan Y down) at 90 degrees");
    expect(near(ndc({16.F / 9.F, 0.F, -1.F}).x, 1.F), "the aspect ratio widens the view");
}

}

int main()
{
    test_vectors();
    test_quaternions();
    test_matrices();
    test_camera();
    if (g_failures != 0) {
        std::cerr << g_failures << " math assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "math types passed\n";
    return EXIT_SUCCESS;
}
