#include <lykoware/skinning.hpp>

#include <lykoware/animation.hpp>

#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace lykoware::animation {

namespace {

bool refuse(std::string& error, const char* reason)
{
    error = reason;
    return false;
}

bool store(double x, double y, double z, math::Vec3& out)
{
    constexpr double limit = std::numeric_limits<float>::max();
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)
        || std::abs(x) > limit || std::abs(y) > limit || std::abs(z) > limit) {
        return false;
    }
    out = {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
    return true;
}

}

bool deform_mesh(const MeshData& bind, std::span<const std::array<std::uint16_t, 4>> joints,
                 std::span<const math::Vec4> weights, std::span<const math::Mat4> matrices,
                 MeshData& out, std::string& error)
{
    error.clear();
    if (!valid_mesh(bind, error)) {
        return false;
    }
    if (joints.size() != bind.positions.size() || weights.size() != bind.positions.size()) {
        return refuse(error, "skinning needs joints and weights for every vertex");
    }
    if (matrices.empty() || matrices.size() > max_joints) {
        return refuse(error, "skinning needs 1..max_joints matrices");
    }
    for (const auto& matrix : matrices) {
        for (const auto& column : matrix.m) {
            for (const float value : column) {
                if (!std::isfinite(value)) {
                    return refuse(error, "skinning matrices must be finite");
                }
            }
        }
        if (matrix.m[0][3] != 0.F || matrix.m[1][3] != 0.F || matrix.m[2][3] != 0.F || matrix.m[3][3] != 1.F) {
            return refuse(error, "skinning matrices must be affine");
        }
    }
    MeshData candidate = bind;
    for (std::size_t vertex = 0; vertex < bind.positions.size(); ++vertex) {
        const auto weight = weights[vertex];
        const double values[4] = {weight.x, weight.y, weight.z, weight.w};
        double sum = 0.0;
        for (std::size_t slot = 0; slot < 4; ++slot) {
            if (!std::isfinite(values[slot]) || values[slot] < 0.0) {
                return refuse(error, "skinning weights must be finite and non-negative");
            }
            if (values[slot] > 0.0 && joints[vertex][slot] >= matrices.size()) {
                return refuse(error, "a weighted skinning joint is outside the palette");
            }
            sum += values[slot];
        }
        if (std::abs(sum - 1.0) > 1e-4) {
            return refuse(error, "skinning weights must sum to 1 within 1e-4");
        }
        // Column-major affine matrix, blended in double to keep extreme finite inputs safe.
        double matrix[4][3]{};
        for (std::size_t slot = 0; slot < 4; ++slot) {
            if (values[slot] == 0.0) {
                continue;
            }
            const auto& joint = matrices[joints[vertex][slot]];
            for (std::size_t column = 0; column < 4; ++column) {
                for (std::size_t row = 0; row < 3; ++row) {
                    matrix[column][row] += static_cast<double>(joint.m[column][row]) * (values[slot] / sum);
                }
            }
        }
        const auto p = bind.positions[vertex];
        if (!store(matrix[0][0] * p.x + matrix[1][0] * p.y + matrix[2][0] * p.z + matrix[3][0],
                   matrix[0][1] * p.x + matrix[1][1] * p.y + matrix[2][1] * p.z + matrix[3][1],
                   matrix[0][2] * p.x + matrix[1][2] * p.y + matrix[2][2] * p.z + matrix[3][2],
                   candidate.positions[vertex])) {
            return refuse(error, "a skinned position is outside finite float range");
        }
        // Cofactor columns = cross(b,c), cross(c,a), cross(a,b). Divide by the determinant
        // for inverse transpose, then normalise. Double covers all products of float inputs.
        double cofactors[3][3]{};
        for (std::size_t column = 0; column < 3; ++column) {
            const auto& a = matrix[(column + 1) % 3];
            const auto& b = matrix[(column + 2) % 3];
            cofactors[column][0] = a[1] * b[2] - a[2] * b[1];
            cofactors[column][1] = a[2] * b[0] - a[0] * b[2];
            cofactors[column][2] = a[0] * b[1] - a[1] * b[0];
        }
        const double determinant = matrix[0][0] * cofactors[0][0] + matrix[0][1] * cofactors[0][1]
                                 + matrix[0][2] * cofactors[0][2];
        if (determinant == 0.0) {
            return refuse(error, "a blended skinning matrix is singular");
        }
        const auto n = bind.normals[vertex];
        // Only the sign of 1/determinant survives normalisation. Avoid huge reciprocals.
        const double sign = determinant < 0.0 ? -1.0 : 1.0;
        const double x = sign * (cofactors[0][0] * n.x + cofactors[1][0] * n.y + cofactors[2][0] * n.z);
        const double y = sign * (cofactors[0][1] * n.x + cofactors[1][1] * n.y + cofactors[2][1] * n.z);
        const double z = sign * (cofactors[0][2] * n.x + cofactors[1][2] * n.y + cofactors[2][2] * n.z);
        const double length = std::hypot(x, y, z);
        if (!(length > 0.0) || !store(x / length, y / length, z / length, candidate.normals[vertex])) {
            return refuse(error, "a skinned normal cannot be normalised");
        }
    }
    out = std::move(candidate);
    return true;
}

}
