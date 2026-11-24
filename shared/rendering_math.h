#pragma once

#include <array>
#include <glm/glm.hpp>

namespace learning {

struct FrustumPlane {
    glm::vec3 inwardNormal; // Points toward the inside of the frustum.
    float offset;          // Plane value: dot(inwardNormal, point) + offset.
};

using FrustumPlanes = std::array<FrustumPlane, 6>;

struct WorldBounds {
    glm::vec3 center;      // Midpoint in world space.
    glm::vec3 halfExtents; // Nonnegative half-width, half-height, and half-depth.
};

// center and nonnegative halfExtents describe a world-space box.
// Returns false only when one inward-facing plane excludes the entire box.
bool isBoxPotentiallyVisible(const glm::vec3& center,
    const glm::vec3& halfExtents, const FrustumPlanes& planes);

// viewProjection = projection * view, using Vulkan depth in [0, W].
// Returns unnormalized planes: left, right, Y lower, Y upper, depth lower, depth upper.
// Inputs must be finite and nondegenerate; infinite-far projections are out of scope.
FrustumPlanes extractFrustumPlanes(const glm::mat4& viewProjection);

// Transform local center and nonnegative half-extents by a finite affine matrix.
// Returns the world-axis-aligned enclosure of all eight transformed corners.
WorldBounds computeWorldBounds(const glm::vec3& localCenter,
    const glm::vec3& localHalfExtents, const glm::mat4& worldTransform);

} // namespace learning
