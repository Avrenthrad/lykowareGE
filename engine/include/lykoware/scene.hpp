#pragma once

#include <lykoware/ecs.hpp>
#include <lykoware/physics.hpp>
#include <lykoware/render.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace lykoware::scene {

// Version-1 built-in box/cube schema (ADR 0031): metres, m/s, radians, linear RGB.
// Authored IDs are independent of ECS handles.
struct Box {
    std::string id;
    physics::DVec3 position;
    physics::Vec3 half_extents{0.5F, 0.5F, 0.5F};
    physics::Vec3 colour{0.8F, 0.8F, 0.8F};
    physics::MotionType motion = physics::MotionType::Dynamic;
};
struct Description {
    std::vector<Box> entities;
    render::Camera camera;
};
struct Destroy { std::string id; };
struct SetVelocity {
    std::string id;
    physics::Vec3 linear;
};
struct Command {
    std::uint64_t tick = 0;
    std::uint64_t sequence = 0;
    std::variant<Box, Destroy, SetVelocity> action;
};

// Return false and leave output unchanged on malformed input. Allocation errors propagate.
[[nodiscard]] bool decode(std::string_view text, Description& output, std::string& error);
[[nodiscard]] bool decode_commands(std::string_view text, std::vector<Command>& output, std::string& error);
// Canonical Lykoware JSON v1; rejects invalid in-memory values with std::invalid_argument.
[[nodiscard]] std::string encode(const Description& description);
[[nodiscard]] std::string encode_commands(const std::vector<Command>& commands);

// Owns physics before ECS (cleanup order matters), stable-ID mapping and tick command handler.
// Single thread; no copy/move. No external physics mutation is exposed. Failed dispatch leaves
// its target unchanged; earlier commands are retained. Exceptions propagate after rollback.
class Scene {
public:
    [[nodiscard]] static std::unique_ptr<Scene> create(const Description& description, std::string& error);
    ~Scene();
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) = delete;
    Scene& operator=(Scene&&) = delete;

    [[nodiscard]] bool dispatch(const Command& command, std::string& error);
    [[nodiscard]] bool advance_tick(std::string& error);
    [[nodiscard]] std::uint64_t tick() const;
    [[nodiscard]] std::uint64_t state_hash() const;
    [[nodiscard]] const ecs::World& entities() const;
    [[nodiscard]] const render::Camera& camera() const;
    // Render-only snapshot: interpolation never modifies the authoritative world.
    [[nodiscard]] ecs::World presentation(double alpha) const;

private:
    struct Impl;
    explicit Scene(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}
