#include <lykoware/streaming_chunks.hpp>

namespace lykoware::streaming {
ChunkCollision physics_chunk_collision(physics::World& world)
{
    return {
        [&world](const physics::BodyDesc& desc, std::string& error) { return world.create_body(desc, error); },
        [&world](physics::BodyId body) { (void)world.destroy_body(body); }
    };
}
}
