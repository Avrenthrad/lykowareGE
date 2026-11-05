#pragma once

// The Phase 2 obstacle course (ADR 0047), shared by the sandbox's --play mode and the
// player.course test, so the course a person plays is the course the test runs. Header only;
// it uses only the engine's public API.
//
// It runs from the start at z = 21 towards -Z, then turns to +X through a tunnel:
//   z 18..8     rolling terrain, up to 0.3 m high (slopes up to about 11 degrees)
//   z 4.5..0    a 20 degree ramp up to a 1.5 m platform (z 0..-4), and a drop off its end
//   z -9..-14   three 0.3 m stairs up to a 1.2 m platform
//   z -14..-18  a 0.6 m ledge, too tall to step up, onto a 1.8 m platform, and a drop off its end
//   x 3..13     a 3 m wide tunnel along +X at z = -20.5 with a 2.4 m ceiling
//   x 16..      a 60 degree slope, too steep to walk up
// Everything stands on a 64 x 64 m height field, flat outside the rolling part, from x, z = -32
// to 31.5.

#include <lykoware/math.hpp>
#include <lykoware/physics.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <string>
#include <vector>

namespace obstacle_course {

namespace math = lykoware::math;
namespace physics = lykoware::physics;

// A static box of the course.
struct Block {
    math::DVec3 centre;
    math::Vec3 half;
    math::Quat rotation;
    math::Vec3 colour;
};

// Where a character starts: its feet, just above the ground, facing -Z.
inline constexpr math::DVec3 start{0.0, 0.05, 21.0};

inline constexpr std::uint32_t terrain_samples = 128;
inline constexpr float terrain_spacing = 0.5F;
inline constexpr math::DVec3 terrain_origin{-32.0, 0.0, -32.0};

// The ground's height at (x, z): rolling between z = 8 and 18, 0 elsewhere.
inline float terrain_height(double x, double z)
{
    if (z <= 8.0 || z >= 18.0) {
        return 0.F;
    }
    const double across = std::sin(std::numbers::pi * (z - 8.0) / 5.0);
    return static_cast<float>(0.3 * across * across * (0.6 + 0.4 * std::sin(0.7 * x)));
}

// The height field's samples (ADR 0020), placed at terrain_origin.
inline physics::HeightFieldShape terrain()
{
    physics::HeightFieldShape field;
    field.samples_per_side = terrain_samples;
    field.spacing = terrain_spacing;
    field.heights.resize(static_cast<std::size_t>(terrain_samples) * terrain_samples);
    for (std::uint32_t z = 0; z < terrain_samples; ++z) {
        for (std::uint32_t x = 0; x < terrain_samples; ++x) {
            field.heights[static_cast<std::size_t>(z) * terrain_samples + x] =
                terrain_height(terrain_origin.x + static_cast<double>(x) * terrain_spacing,
                               terrain_origin.z + static_cast<double>(z) * terrain_spacing);
        }
    }
    return field;
}

inline std::vector<Block> blocks()
{
    constexpr float pi = std::numbers::pi_v<float>;
    constexpr math::Vec3 stone{0.62F, 0.6F, 0.56F};
    constexpr math::Vec3 wood{0.62F, 0.45F, 0.3F};
    constexpr math::Vec3 brick{0.66F, 0.36F, 0.3F};
    std::vector<Block> course;
    // A 0.2 m thick slab whose top face starts at `from` and runs `length` along `along`, tilted
    // by `rotation`; `along` is the slab's local X when `local_x`, else its local Z.
    const auto slab = [&course](math::DVec3 from, math::Vec3 along, math::Vec3 normal, float length,
                                math::Quat rotation, bool local_x, math::Vec3 colour) {
        const float half = length / 2.F;
        course.push_back({from + along * half + normal * -0.1F,
                          local_x ? math::Vec3{half, 0.1F, 2.F} : math::Vec3{2.F, 0.1F, half}, rotation, colour});
    };

    // The ramp rises towards -Z to the platform's edge at (z 0, y 1.5) and runs 0.4 m into the
    // ground at its foot, so neither end has a lip.
    const float ramp = 20.F * pi / 180.F;
    slab({0.0, 1.5, 0.0}, {0.F, -std::sin(ramp), std::cos(ramp)}, {0.F, std::cos(ramp), std::sin(ramp)},
         1.5F / std::sin(ramp) + 0.4F, math::from_axis_angle({1.F, 0.F, 0.F}, ramp), false, wood);
    course.push_back({{0.0, 0.75, -2.0}, {2.F, 0.75F, 2.F}, {}, stone});
    // Stairs: 0.3 m steps, 0.6 m deep; the fourth is the 1.2 m platform.
    for (int step = 1; step <= 3; ++step) {
        course.push_back({{0.0, 0.15 * step, -9.0 - 0.6 * (step - 0.5)},
                          {2.F, 0.15F * static_cast<float>(step), 0.3F},
                          {},
                          wood});
    }
    course.push_back({{0.0, 0.6, -12.4}, {2.F, 0.6F, 1.6F}, {}, stone});
    course.push_back({{0.0, 0.9, -16.0}, {2.F, 0.9F, 2.F}, {}, stone});
    // The tunnel: two walls and a ceiling.
    course.push_back({{8.0, 1.3, -18.85}, {5.F, 1.3F, 0.15F}, {}, brick});
    course.push_back({{8.0, 1.3, -22.15}, {5.F, 1.3F, 0.15F}, {}, brick});
    course.push_back({{8.0, 2.55, -20.5}, {5.F, 0.15F, 1.8F}, {}, brick});
    // The steep slope rises towards +X from x = 16, 3 m high, and runs 0.4 m into the ground.
    const float steep = 60.F * pi / 180.F;
    const math::Vec3 uphill{std::cos(steep), std::sin(steep), 0.F};
    slab(math::DVec3{16.0, 0.0, -20.5} + uphill * -0.4F, uphill, {-std::sin(steep), std::cos(steep), 0.F},
         3.F / std::sin(steep) + 0.4F, math::from_axis_angle({0.F, 0.F, 1.F}, steep), true, stone);
    return course;
}

// Adds the terrain and every block to `world` as static bodies; false, with error filled, if
// the world refuses one.
inline bool build(physics::World& world, std::string& error)
{
    physics::BodyDesc ground;
    ground.shape = terrain();
    ground.motion = physics::MotionType::Static;
    ground.position = terrain_origin;
    if (!world.create_body(ground, error)) {
        return false;
    }
    for (const Block& block : blocks()) {
        physics::BodyDesc desc;
        desc.shape = physics::BoxShape{block.half};
        desc.motion = physics::MotionType::Static;
        desc.position = block.centre;
        desc.rotation = block.rotation;
        if (!world.create_body(desc, error)) {
            return false;
        }
    }
    return true;
}

}
