#include <lykoware/foliage.hpp>

#include <cmath>
#include <cstddef>
#include <utility>

namespace lykoware::render {

namespace {

// splitmix64: small, fast, and the same on every platform.
struct Random {
    std::uint64_t state;

    std::uint64_t next()
    {
        state += 0x9E3779B97F4A7C15ULL;
        std::uint64_t z = state;
        z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31U);
    }

    // Evenly in [0, 1), from 24 bits so the float is exact.
    float unit()
    {
        return static_cast<float>(next() >> 40U) * (1.F / 16777216.F);
    }
};

}

bool scatter_foliage(const FoliageScatter& scatter, std::vector<FoliageInstance>& instances, std::string& error)
{
    TerrainPatch ground = scatter.ground;
    ground.step = 1;
    ground.texture_metres = 1.F;
    const std::size_t side = ground.samples_per_side;
    // The same checks terrain_mesh makes, without building a mesh.
    if (side < 2 || ground.heights.size() != side * side) {
        error = "foliage ground needs at least 2 samples per side and samples_per_side squared heights";
        return false;
    }
    if (!std::isfinite(ground.spacing) || ground.spacing <= 0.F) {
        error = "foliage ground spacing must be finite and above 0";
        return false;
    }
    for (const float h : ground.heights) {
        if (!std::isfinite(h)) {
            error = "foliage ground heights must be finite";
            return false;
        }
    }
    if (!std::isfinite(scatter.density) || scatter.density <= 0.F) {
        error = "foliage density must be finite and above 0";
        return false;
    }
    if (!std::isfinite(scatter.max_slope) || scatter.max_slope < 0.F || scatter.max_slope > 1.5707964F) {
        error = "foliage max_slope must be 0 .. pi/2";
        return false;
    }
    if (!std::isfinite(scatter.min_scale) || !std::isfinite(scatter.max_scale) || scatter.min_scale <= 0.F
        || scatter.max_scale < scatter.min_scale) {
        error = "foliage scales must be finite with 0 < min_scale <= max_scale";
        return false;
    }
    const float spacing = ground.spacing;
    const double per_cell = static_cast<double>(scatter.density) * spacing * spacing;
    const std::size_t cells = side - 1;
    if (per_cell * static_cast<double>(cells) * static_cast<double>(cells) > static_cast<double>(foliage_max_instances)) {
        error = "foliage scatter is past foliage_max_instances; lower the density or use a smaller patch";
        return false;
    }
    const auto whole = static_cast<std::size_t>(per_cell);
    const float fraction = static_cast<float>(per_cell - static_cast<double>(whole));
    const float min_up = std::cos(scatter.max_slope);
    const auto height = [&](std::size_t x, std::size_t z) { return ground.heights[z * side + x]; };

    std::vector<FoliageInstance> made;
    for (std::size_t cz = 0; cz < cells; ++cz) {
        for (std::size_t cx = 0; cx < cells; ++cx) {
            Random random{scatter.seed ^ ((cz * side + cx + 1) * 0xD6E8FEB86659FD93ULL)};
            static_cast<void>(random.next());
            const std::size_t count = whole + (random.unit() < fraction ? 1U : 0U);
            const float h00 = height(cx, cz);
            const float h10 = height(cx + 1, cz);
            const float h01 = height(cx, cz + 1);
            const float h11 = height(cx + 1, cz + 1);
            for (std::size_t i = 0; i < count; ++i) {
                // Every plant draws all four numbers, kept or not, so the rest do not shift.
                const float u = random.unit();
                const float v = random.unit();
                const float yaw = random.unit() * 6.2831853F;
                const float scale = scatter.min_scale + random.unit() * (scatter.max_scale - scatter.min_scale);
                // The cell's two triangles, split as terrain_mesh splits them.
                const bool far_triangle = v >= u;
                const float rise_x = far_triangle ? h11 - h01 : h10 - h00;
                const float rise_z = far_triangle ? h01 - h00 : h11 - h10;
                const float slope_x = rise_x / spacing;
                const float slope_z = rise_z / spacing;
                const float up = 1.F / std::sqrt(1.F + slope_x * slope_x + slope_z * slope_z);
                if (up < min_up) {
                    continue;
                }
                made.push_back({{(static_cast<float>(cx) + u) * spacing, h00 + u * rise_x + v * rise_z,
                                 (static_cast<float>(cz) + v) * spacing},
                                yaw, scale});
            }
        }
    }
    instances = std::move(made);
    return true;
}

bool foliage_batch(const MeshData& plant, std::span<const FoliageInstance> instances, MeshData& batch, std::string& error)
{
    std::string problem;
    if (!valid_mesh(plant, problem)) {
        error = "foliage plant: " + problem;
        return false;
    }
    if (instances.empty() || instances.size() > foliage_max_instances) {
        error = "a foliage batch needs 1 .. foliage_max_instances instances";
        return false;
    }
    if (plant.positions.size() > mesh_max_vertices / instances.size()
        || plant.indices.size() > mesh_max_indices / instances.size()) {
        error = "foliage batch is past the mesh limits; use fewer instances per batch";
        return false;
    }
    for (const FoliageInstance& instance : instances) {
        if (!std::isfinite(instance.position.x) || !std::isfinite(instance.position.y) || !std::isfinite(instance.position.z)
            || !std::isfinite(instance.yaw) || !std::isfinite(instance.scale) || instance.scale <= 0.F) {
            error = "a foliage instance needs a finite position and yaw and a finite scale above 0";
            return false;
        }
    }
    MeshData made;
    made.positions.reserve(plant.positions.size() * instances.size());
    made.normals.reserve(plant.normals.size() * instances.size());
    made.uvs.reserve(plant.uvs.size() * instances.size());
    made.indices.reserve(plant.indices.size() * instances.size());
    for (const FoliageInstance& instance : instances) {
        const auto base = static_cast<std::uint32_t>(made.positions.size());
        const float c = std::cos(instance.yaw);
        const float s = std::sin(instance.yaw);
        // A turn about +Y by yaw: +X goes towards -Z, as a right-handed rotation.
        for (const math::Vec3& p : plant.positions) {
            made.positions.push_back({instance.position.x + instance.scale * (c * p.x + s * p.z),
                                      instance.position.y + instance.scale * p.y,
                                      instance.position.z + instance.scale * (c * p.z - s * p.x)});
        }
        for (const math::Vec3& n : plant.normals) {
            made.normals.push_back({c * n.x + s * n.z, n.y, c * n.z - s * n.x});
        }
        made.uvs.insert(made.uvs.end(), plant.uvs.begin(), plant.uvs.end());
        for (const std::uint32_t index : plant.indices) {
            made.indices.push_back(base + index);
        }
    }
    batch = std::move(made);
    return true;
}

}
