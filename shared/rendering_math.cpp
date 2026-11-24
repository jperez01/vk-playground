#include "rendering_math.h"

#include <cstddef>

namespace learning {

// Box midpoint and nonnegative half-sizes are in world space.
// Reject only when one inward-facing plane excludes the entire box.
bool isBoxPotentiallyVisible(
    const glm::vec3& center,
    const glm::vec3& halfExtents,
    const FrustumPlanes& planes) {
    for (const auto& plane : planes)
    {
        auto centerValue = glm::dot(plane.inwardNormal, center) + plane.offset;
        auto radius = glm::dot(glm::abs(plane.inwardNormal) , halfExtents);

        auto largestValue = centerValue + radius;

        if (largestValue < 0) return false;
    }

    return true;
}

// viewProjection = projection * view, with Vulkan depth in [0, W].
// Plane order: left, right, Y lower, Y upper, depth lower, depth upper.
// Inputs are finite and nondegenerate; infinite-far projections are out of scope.
// Each row maps a world-space point to one clip-space component.
// Combining rows rewrites a clip boundary as a world-space plane equation.
FrustumPlanes extractFrustumPlanes(const glm::mat4& viewProjection)
{
    // GLM indexes [column][row], so gather each row explicitly.
    const glm::vec4 rowX(viewProjection[0][0], viewProjection[1][0],
                         viewProjection[2][0], viewProjection[3][0]);
    const glm::vec4 rowY(viewProjection[0][1], viewProjection[1][1],
                         viewProjection[2][1], viewProjection[3][1]);
    const glm::vec4 rowZ(viewProjection[0][2], viewProjection[1][2],
                         viewProjection[2][2], viewProjection[3][2]);
    const glm::vec4 rowW(viewProjection[0][3], viewProjection[1][3],
                         viewProjection[2][3], viewProjection[3][3]);

    const auto makePlane = [](const glm::vec4& coefficients) {
        return FrustumPlane{glm::vec3(coefficients), coefficients.w};
    };

    // Vulkan clip inequalities are nonnegative on the inside.
    // Depth names remain lower/upper because reversed-Z exchanges near/far.
    return {{
        makePlane(rowW + rowX), // clipW + clipX >= 0: left
        makePlane(rowW - rowX), // clipW - clipX >= 0: right
        makePlane(rowW + rowY), // clipW + clipY >= 0: Y lower
        makePlane(rowW - rowY), // clipW - clipY >= 0: Y upper
        makePlane(rowZ),        // clipZ >= 0: depth lower
        makePlane(rowW - rowZ), // clipW - clipZ >= 0: depth upper
    }};
}

// Transform local bounds by a finite affine local-to-world matrix.
// The result encloses all eight transformed corners; half-extents stay nonnegative.
WorldBounds computeWorldBounds(
    const glm::vec3& localCenter,
    const glm::vec3& localHalfExtents,
    const glm::mat4& worldTransform)
{
    std::array<glm::vec3, 8> corners;
    std::size_t cornerIndex = 0;
    for (int i = -1; i <= 1; i += 2)
    {
        for (int j = -1; j <= 1; j += 2)
        {
            for (int k = -1; k <= 1; k += 2)
            {
                corners[cornerIndex++] = localCenter + glm::vec3(i, j, k) * localHalfExtents;
            }
        }
    }

    glm::vec4 smallestCoords = worldTransform * glm::vec4(corners[0], 1.0f);
    glm::vec4 largestCoords = smallestCoords;

    for (std::size_t index = 1; index < corners.size(); ++index)
    {
        auto transformedCorner = worldTransform * glm::vec4(corners[index], 1.0f);
        largestCoords = glm::max(transformedCorner, largestCoords);
        smallestCoords = glm::min(transformedCorner, smallestCoords);
    }

    auto worldCenter = (smallestCoords + largestCoords) / 2.0f;
    auto worldHalfExtents = (largestCoords - smallestCoords) / 2.0f;

    return WorldBounds(worldCenter, worldHalfExtents);
}

} // namespace learning
