#include <lykoware/distant_detail.hpp>

#include <algorithm>
#include <cmath>

namespace lykoware::render {

namespace {

double axis_gap(double value, double low, double high)
{
    if (value < low) {
        return low - value;
    }
    if (value > high) {
        return value - high;
    }
    return 0.0;
}

bool bands_valid(const ChunkDetailBands& bands, std::string& error)
{
    if (!std::isfinite(bands.far_from_metres) || bands.far_from_metres <= 0.0) {
        error = "chunk detail far_from_metres must be finite and above 0";
        return false;
    }
    if (!std::isfinite(bands.hysteresis_metres) || bands.hysteresis_metres < 0.0
        || bands.hysteresis_metres >= bands.far_from_metres) {
        error = "chunk detail hysteresis_metres must be at least 0 and below far_from_metres";
        return false;
    }
    if (bands.far_step < 2) {
        error = "chunk detail far_step must be above 1";
        return false;
    }
    return true;
}

}

double chunk_distance(streaming::Region region, const math::DVec3& eye)
{
    const streaming::Bounds bounds = streaming::region_bounds(region);
    const double dx = axis_gap(eye.x, bounds.min_x, bounds.max_x);
    const double dz = axis_gap(eye.z, bounds.min_z, bounds.max_z);
    return std::sqrt(dx * dx + dz * dz);
}

math::DVec3 chunk_origin(streaming::Region region)
{
    const streaming::Bounds bounds = streaming::region_bounds(region);
    return {bounds.min_x, 0.0, bounds.min_z};
}

bool choose_chunk_detail(const ChunkDetailBands& bands, double distance, ChunkDetail previous,
                         ChunkDetail& detail, std::string& error)
{
    if (!bands_valid(bands, error)) {
        return false;
    }
    if (!std::isfinite(distance) || distance < 0.0) {
        error = "chunk distance must be finite and at least 0";
        return false;
    }
    if (previous == ChunkDetail::far) {
        detail = distance < bands.far_from_metres - bands.hysteresis_metres ? ChunkDetail::near
                                                                             : ChunkDetail::far;
    } else {
        detail = distance >= bands.far_from_metres ? ChunkDetail::far : ChunkDetail::near;
    }
    return true;
}

bool chunk_terrain_mesh(TerrainPatch patch, ChunkDetail detail, const ChunkDetailBands& bands,
                        MeshData& mesh, std::string& error)
{
    if (!bands_valid(bands, error)) {
        return false;
    }
    patch.step = detail == ChunkDetail::far ? bands.far_step : 1U;
    return terrain_mesh(patch, mesh, error);
}

}
