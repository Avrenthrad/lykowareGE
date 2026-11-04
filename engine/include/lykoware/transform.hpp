#pragma once

#include <lykoware/math.hpp>

namespace lykoware {

// Where an entity is in the world (ADR 0026): a double-precision position and a rotation.
// Physics writes it (physics_ecs::Link::sync) and the renderer reads it, camera-relative.
struct Transform {
    math::DVec3 position;
    math::Quat rotation;
};

}
