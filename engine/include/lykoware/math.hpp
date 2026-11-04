#pragma once

#include <cmath>

// Lykoware's maths types (ADR 0024): vectors, quaternions and 4x4 matrices. Lykoware's own
// code, no third-party library.
//
// Conventions: right-handed, +Y up, metres and radians. A camera looks down its -Z axis.
// Matrices are column-major (m[column][row]) and multiply column vectors (p' = M * p), which
// is what Vulkan and Slang expect. Projection is Vulkan's: clip-space Y points down and depth
// runs 0..1, with reversed depth (near = 1, far = 0) for precision across a 16 km world.
//
// Determinism: +, -, *, / and sqrt round identically on every supported compiler and CPU
// (IEEE 754). The trigonometric helpers (from_axis_angle, perspective) call the C library,
// whose results can differ in the last bit between platforms: use them for cameras and
// drawing, not for simulation state that must replay identically.

namespace lykoware::math {

struct Vec2 {
    float x = 0.F;
    float y = 0.F;
};

struct Vec3 {
    float x = 0.F;
    float y = 0.F;
    float z = 0.F;
};

struct Vec4 {
    float x = 0.F;
    float y = 0.F;
    float z = 0.F;
    float w = 0.F;
};

// A position in the world, in double precision (ADR 0022).
struct DVec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

// Unit quaternion (x, y, z, w); the default is no rotation.
struct Quat {
    float x = 0.F;
    float y = 0.F;
    float z = 0.F;
    float w = 1.F;
};

// Column-major 4x4 matrix: m[column][row]. The default is the identity.
struct Mat4 {
    float m[4][4] = {{1.F, 0.F, 0.F, 0.F}, {0.F, 1.F, 0.F, 0.F}, {0.F, 0.F, 1.F, 0.F}, {0.F, 0.F, 0.F, 1.F}};
};

// --- Vec3 ---------------------------------------------------------------------------------

constexpr Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
constexpr Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
constexpr Vec3 operator-(Vec3 a) { return {-a.x, -a.y, -a.z}; }
constexpr Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
constexpr Vec3 operator*(float s, Vec3 a) { return a * s; }
constexpr Vec3 operator/(Vec3 a, float s) { return {a.x / s, a.y / s, a.z / s}; }
constexpr bool operator==(Vec3 a, Vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
constexpr float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
constexpr Vec3 cross(Vec3 a, Vec3 b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(Vec3 a) { return std::sqrt(dot(a, a)); }
// The unit vector along a; the zero vector stays zero.
inline Vec3 normalize(Vec3 a)
{
    const float len = length(a);
    return len > 0.F ? a / len : Vec3{};
}
constexpr Vec3 lerp(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }

// --- DVec3 --------------------------------------------------------------------------------

constexpr DVec3 operator+(DVec3 a, DVec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
constexpr DVec3 operator-(DVec3 a, DVec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
constexpr DVec3 operator+(DVec3 a, Vec3 b)
{
    return {a.x + static_cast<double>(b.x), a.y + static_cast<double>(b.y), a.z + static_cast<double>(b.z)};
}
constexpr bool operator==(DVec3 a, DVec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
// The offset from `origin` to `p`, computed in double and then rounded to float: exact to
// float precision near the origin, wherever both points are. This is how world positions
// reach the GPU (camera-relative rendering, ADR 0021).
constexpr Vec3 relative_to(DVec3 p, DVec3 origin)
{
    return {static_cast<float>(p.x - origin.x), static_cast<float>(p.y - origin.y), static_cast<float>(p.z - origin.z)};
}

// --- Quat ---------------------------------------------------------------------------------

// Rotation of `angle` radians about `axis` (normalised here; a zero axis gives no rotation).
inline Quat from_axis_angle(Vec3 axis, float angle)
{
    const Vec3 n = normalize(axis);
    if (n == Vec3{}) {
        return {};
    }
    const float s = std::sin(angle * 0.5F);
    return {n.x * s, n.y * s, n.z * s, std::cos(angle * 0.5F)};
}
// a * b applies b first, then a.
constexpr Quat operator*(Quat a, Quat b)
{
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
constexpr Quat conjugate(Quat q) { return {-q.x, -q.y, -q.z, q.w}; }
// The unit quaternion along q; a zero quaternion becomes no rotation.
inline Quat normalize(Quat q)
{
    const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return len > 0.F ? Quat{q.x / len, q.y / len, q.z / len, q.w / len} : Quat{};
}
// v rotated by the unit quaternion q.
constexpr Vec3 rotate(Quat q, Vec3 v)
{
    const Vec3 u{q.x, q.y, q.z};
    const Vec3 t = cross(u, v) * 2.F;
    return v + t * q.w + cross(u, t);
}

// --- Mat4 ---------------------------------------------------------------------------------

constexpr Mat4 operator*(const Mat4& a, const Mat4& b)
{
    Mat4 r;
    for (int c = 0; c < 4; ++c) {
        for (int row = 0; row < 4; ++row) {
            r.m[c][row] = a.m[0][row] * b.m[c][0] + a.m[1][row] * b.m[c][1] + a.m[2][row] * b.m[c][2]
                        + a.m[3][row] * b.m[c][3];
        }
    }
    return r;
}
constexpr Vec4 operator*(const Mat4& a, Vec4 v)
{
    return {a.m[0][0] * v.x + a.m[1][0] * v.y + a.m[2][0] * v.z + a.m[3][0] * v.w,
            a.m[0][1] * v.x + a.m[1][1] * v.y + a.m[2][1] * v.z + a.m[3][1] * v.w,
            a.m[0][2] * v.x + a.m[1][2] * v.y + a.m[2][2] * v.z + a.m[3][2] * v.w,
            a.m[0][3] * v.x + a.m[1][3] * v.y + a.m[2][3] * v.z + a.m[3][3] * v.w};
}
// The point p transformed by a (w = 1, no perspective divide).
constexpr Vec3 transform_point(const Mat4& a, Vec3 p)
{
    const Vec4 r = a * Vec4{p.x, p.y, p.z, 1.F};
    return {r.x, r.y, r.z};
}
constexpr Mat4 translation(Vec3 t)
{
    Mat4 r;
    r.m[3][0] = t.x;
    r.m[3][1] = t.y;
    r.m[3][2] = t.z;
    return r;
}
constexpr Mat4 scaling(Vec3 s)
{
    Mat4 r;
    r.m[0][0] = s.x;
    r.m[1][1] = s.y;
    r.m[2][2] = s.z;
    return r;
}
// The rotation matrix of the unit quaternion q.
constexpr Mat4 rotation(Quat q)
{
    const float xx = q.x * q.x;
    const float yy = q.y * q.y;
    const float zz = q.z * q.z;
    const float xy = q.x * q.y;
    const float xz = q.x * q.z;
    const float yz = q.y * q.z;
    const float wx = q.w * q.x;
    const float wy = q.w * q.y;
    const float wz = q.w * q.z;
    Mat4 r;
    r.m[0][0] = 1.F - 2.F * (yy + zz);
    r.m[0][1] = 2.F * (xy + wz);
    r.m[0][2] = 2.F * (xz - wy);
    r.m[1][0] = 2.F * (xy - wz);
    r.m[1][1] = 1.F - 2.F * (xx + zz);
    r.m[1][2] = 2.F * (yz + wx);
    r.m[2][0] = 2.F * (xz + wy);
    r.m[2][1] = 2.F * (yz - wx);
    r.m[2][2] = 1.F - 2.F * (xx + yy);
    return r;
}
// Translation * rotation * scale: scale first, then rotate, then move.
constexpr Mat4 trs(Vec3 t, Quat q, Vec3 s) { return translation(t) * rotation(q) * scaling(s); }

// Inverse of a rigid transform (rotation and translation only, no scale): the view matrix of
// a camera placed by `camera_to_world`.
constexpr Mat4 rigid_inverse(const Mat4& a)
{
    Mat4 r;
    for (int c = 0; c < 3; ++c) {
        for (int row = 0; row < 3; ++row) {
            r.m[c][row] = a.m[row][c];
        }
    }
    for (int row = 0; row < 3; ++row) {
        r.m[3][row] = -(r.m[0][row] * a.m[3][0] + r.m[1][row] * a.m[3][1] + r.m[2][row] * a.m[3][2]);
    }
    return r;
}

// View matrix of a camera at `eye` looking at `target` with `up` roughly up. The camera
// looks down its -Z axis. Positions are relative (see relative_to): the camera is usually at
// the origin of its own frame. Degenerate inputs (target at eye, up along the view) give the
// identity.
inline Mat4 look_at(Vec3 eye, Vec3 target, Vec3 up)
{
    const Vec3 forward = normalize(target - eye);
    const Vec3 right = normalize(cross(forward, up));
    if (forward == Vec3{} || right == Vec3{}) {
        return {};
    }
    const Vec3 true_up = cross(right, forward);
    Mat4 camera;
    camera.m[0][0] = right.x;
    camera.m[0][1] = right.y;
    camera.m[0][2] = right.z;
    camera.m[1][0] = true_up.x;
    camera.m[1][1] = true_up.y;
    camera.m[1][2] = true_up.z;
    camera.m[2][0] = -forward.x;
    camera.m[2][1] = -forward.y;
    camera.m[2][2] = -forward.z;
    camera.m[3][0] = eye.x;
    camera.m[3][1] = eye.y;
    camera.m[3][2] = eye.z;
    return rigid_inverse(camera);
}

// Vulkan perspective projection with reversed, infinite depth: a point `near` metres in front
// of the camera gets depth 1, and depth falls towards 0 with distance, never reaching it.
// Clip-space Y points down (Vulkan), so view-space +Y appears at the top of the image.
// vertical_fov in radians, aspect = width / height, near > 0.
inline Mat4 perspective(float vertical_fov, float aspect, float near)
{
    const float f = 1.F / std::tan(vertical_fov * 0.5F);
    Mat4 r;
    r.m[0][0] = f / aspect;
    r.m[1][1] = -f;
    r.m[2][2] = 0.F;
    r.m[2][3] = -1.F;
    r.m[3][2] = near;
    r.m[3][3] = 0.F;
    return r;
}

}
