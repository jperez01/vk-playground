#include "rendering_math.h"

#include <cmath>
#include <iostream>
#include <string_view>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace {

int runFrustumChecks()
{
    using learning::FrustumPlanes;

    // Orthographic viewing volume: -5 <= X,Y,Z <= 5.
    const FrustumPlanes boxPlanes{{
        {{1.f, 0.f, 0.f}, 5.f},
        {{-1.f, 0.f, 0.f}, 5.f},
        {{0.f, 1.f, 0.f}, 5.f},
        {{0.f, -1.f, 0.f}, 5.f},
        {{0.f, 0.f, 1.f}, 5.f},
        {{0.f, 0.f, -1.f}, 5.f},
    }};

    int checks = 0;
    int failures = 0;
    const auto check = [&](std::string_view name, const glm::vec3& center,
                           const glm::vec3& halfExtents, const FrustumPlanes& planes,
                           bool expected) {
        ++checks;
        const bool actual = learning::isBoxPotentiallyVisible(center, halfExtents, planes);
        if (actual != expected) {
            ++failures;
            std::cout << "FAIL: " << name << " -- expected "
                      << (expected ? "potentially visible" : "rejected") << ", got "
                      << (actual ? "potentially visible" : "rejected") << '\n';
        } else {
            std::cout << "PASS: " << name << '\n';
        }
    };

    struct BoxCase {
        const char* name;
        glm::vec3 center;
        glm::vec3 halfExtents;
        bool expected;
    };
    const BoxCase cases[]{
        {"fully inside", {0.f, 0.f, 0.f}, {1.f, 1.f, 1.f}, true},
        {"inside away from origin", {2.f, -1.f, 1.f}, {0.5f, 2.f, 1.f}, true},
        {"box encloses the viewing volume", {0.f, 0.f, 0.f}, {10.f, 10.f, 10.f}, true},
        {"straddles right plane despite outside center", {5.5f, 0.f, 0.f}, {1.f, 1.f, 1.f}, true},
        {"outside left", {-7.f, 0.f, 0.f}, {1.f, 1.f, 1.f}, false},
        {"outside right", {7.f, 0.f, 0.f}, {1.f, 1.f, 1.f}, false},
        {"outside bottom", {0.f, -7.f, 0.f}, {1.f, 1.f, 1.f}, false},
        {"outside top", {0.f, 7.f, 0.f}, {1.f, 1.f, 1.f}, false},
        {"outside back", {0.f, 0.f, -7.f}, {1.f, 1.f, 1.f}, false},
        {"outside front", {0.f, 0.f, 7.f}, {1.f, 1.f, 1.f}, false},
        {"touches left", {-6.f, 0.f, 0.f}, {1.f, 1.f, 1.f}, true},
        {"touches right", {6.f, 0.f, 0.f}, {1.f, 1.f, 1.f}, true},
        {"touches bottom", {0.f, -6.f, 0.f}, {1.f, 1.f, 1.f}, true},
        {"touches top", {0.f, 6.f, 0.f}, {1.f, 1.f, 1.f}, true},
        {"touches back", {0.f, 0.f, -6.f}, {1.f, 1.f, 1.f}, true},
        {"touches front", {0.f, 0.f, 6.f}, {1.f, 1.f, 1.f}, true},
        {"point inside", {0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}, true},
        {"point on boundary", {5.f, 0.f, 0.f}, {0.f, 0.f, 0.f}, true},
        {"point outside", {6.f, 0.f, 0.f}, {0.f, 0.f, 0.f}, false},
    };
    for (const auto& test : cases)
        check(test.name, test.center, test.halfExtents, boxPlanes, test.expected);

    // A rotated orthographic volume. The first boundary is X + Y >= 1.5;
    // the other five boundaries are far enough away not to reject these boxes.
    // Intersection requires adding contributions from multiple normal components.
    FrustumPlanes diagonalPlanes{{
        {{1.f, 1.f, 0.f}, -1.5f},
        {{-1.f, -1.f, 0.f}, 10.f},
        {{-1.f, 1.f, 0.f}, 10.f},
        {{1.f, -1.f, 0.f}, 10.f},
        {{0.f, 0.f, 1.f}, 10.f},
        {{0.f, 0.f, -1.f}, 10.f},
    }};
    check("diagonal plane intersects box (radius-sum regression)",
          {0.f, 0.f, 0.f}, {1.f, 1.f, 1.f}, diagonalPlanes, true);
    diagonalPlanes[0].offset = -2.f;
    check("diagonal plane touches box edge",
          {0.f, 0.f, 0.f}, {1.f, 1.f, 1.f}, diagonalPlanes, true);
    diagonalPlanes[0].offset = -2.25f;
    check("diagonal plane fully rejects box",
          {0.f, 0.f, 0.f}, {1.f, 1.f, 1.f}, diagonalPlanes, false);

    // Reflect the volume so the same radius test needs absolute normal components.
    diagonalPlanes[0].offset = -1.5f;
    for (auto& plane : diagonalPlanes)
        plane.inwardNormal = -plane.inwardNormal;
    check("negative diagonal normal intersects box",
          {0.f, 0.f, 0.f}, {1.f, 1.f, 1.f}, diagonalPlanes, true);
    check("negative diagonal normal rejects distant box",
          {3.f, 3.f, 0.f}, {1.f, 1.f, 1.f}, diagonalPlanes, false);

    // Positive scaling changes the plane equation's magnitude, not its boundary.
    FrustumPlanes scaledPlanes = boxPlanes;
    for (auto& plane : scaledPlanes) {
        plane.inwardNormal *= 4.f;
        plane.offset *= 4.f;
    }
    check("unnormalized planes preserve intersection",
          {5.5f, 0.f, 0.f}, {1.f, 1.f, 1.f}, scaledPlanes, true);
    check("unnormalized planes preserve rejection",
          {7.f, 0.f, 0.f}, {1.f, 1.f, 1.f}, scaledPlanes, false);
    check("unnormalized planes preserve touching",
          {6.f, 0.f, 0.f}, {1.f, 1.f, 1.f}, scaledPlanes, true);

    // Canonical Vulkan clip volume: X,Y in [-1,1], Z in [0,1].
    // In particular, negative Z must not be accepted as it would be for OpenGL.
    const auto canonicalPlanes = learning::extractFrustumPlanes(glm::mat4(1.f));
    check("extracted identity keeps interior", {0.f, 0.f, 0.5f}, {0.f, 0.f, 0.f}, canonicalPlanes, true);
    check("extracted identity rejects negative depth", {0.f, 0.f, -0.25f}, {0.f, 0.f, 0.f}, canonicalPlanes, false);
    check("extracted identity rejects excessive depth", {0.f, 0.f, 1.25f}, {0.f, 0.f, 0.f}, canonicalPlanes, false);
    check("extracted identity keeps depth-zero boundary", {0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}, canonicalPlanes, true);
    check("extracted identity keeps depth-one boundary", {0.f, 0.f, 1.f}, {0.f, 0.f, 0.f}, canonicalPlanes, true);
    check("extracted identity keeps side boundary", {1.f, 0.f, 0.5f}, {0.f, 0.f, 0.f}, canonicalPlanes, true);
    check("extracted identity keeps straddling box", {1.25f, 0.f, 0.5f}, {0.5f, 0.25f, 0.25f}, canonicalPlanes, true);

    // Independent reference: project a point and evaluate Vulkan's clip inequalities
    // directly, without extracting planes or repeating the row-combination algorithm.
    const glm::vec3 cameraPoints[]{
        {0.f, 0.f, -1.f}, {0.f, 0.f, -5.f}, {0.f, 0.f, -0.05f},
        {0.f, 0.f, 1.f}, {0.f, 0.f, -60.f},
        {10.f, 0.f, -5.f}, {-10.f, 0.f, -5.f},
        {0.f, 5.f, -5.f}, {0.f, -5.f, -5.f},
        {2.f, 1.f, -5.f}, {-1.f, -0.5f, -5.f},
        {20.f, 0.f, -20.f}, {-20.f, 0.f, -20.f},
    };
    const auto checkExtraction = [&](std::string_view name, const glm::mat4& projection,
                                     const glm::mat4& view) {
        const auto planes = learning::extractFrustumPlanes(projection * view);
        const auto cameraWorld = glm::inverse(view);
        const int failuresBefore = failures;
        for (const auto& cameraPoint : cameraPoints) {
            const glm::vec4 clip = projection * glm::vec4(cameraPoint, 1.f);
            const bool expected = clip.x >= -clip.w && clip.x <= clip.w
                && clip.y >= -clip.w && clip.y <= clip.w
                && clip.z >= 0.f && clip.z <= clip.w;
            const glm::vec3 worldPoint(cameraWorld * glm::vec4(cameraPoint, 1.f));
            const bool actual = learning::isBoxPotentiallyVisible(worldPoint, glm::vec3(0.f), planes);
            ++checks;
            if (actual != expected) {
                ++failures;
                std::cout << "FAIL: " << name << " at camera-space point ("
                          << cameraPoint.x << ", " << cameraPoint.y << ", " << cameraPoint.z
                          << ") -- expected " << expected << ", got " << actual << '\n';
            }
        }
        if (failures == failuresBefore)
            std::cout << "PASS: " << name << " (clip-space reference comparison)\n";
    };
    const glm::mat4 identity(1.f);
    const auto view = glm::lookAtRH(glm::vec3(5.f, 3.f, 8.f),
        glm::vec3(1.f, 0.f, -2.f), glm::vec3(0.f, 1.f, 0.f));
    const auto perspective = glm::perspectiveRH_ZO(glm::radians(70.f), 1.5f, 0.1f, 50.f);
    const auto reversedPerspective = glm::perspectiveRH_ZO(glm::radians(70.f), 1.5f, 50.f, 0.1f);
    checkExtraction("perspective camera at origin", perspective, identity);
    checkExtraction("translated and rotated perspective camera", perspective, view);
    checkExtraction("asymmetric orthographic camera",
        glm::orthoRH_ZO(-2.f, 3.f, -1.f, 4.f, 0.1f, 50.f), view);
    checkExtraction("finite reversed-Z camera", reversedPerspective, view);
    auto flippedProjection = reversedPerspective;
    flippedProjection[1][1] *= -1.f;
    checkExtraction("reversed-Z camera with Vulkan Y flip", flippedProjection, view);

    std::cout << '\n' << checks - failures << '/' << checks << " checks passed.\n";
    return failures == 0 ? 0 : 1;
}

int runWorldBoundsChecks()
{
    const glm::mat4 identity(1.f);
    int checks = 0;
    int failures = 0;
    const auto near = [](const glm::vec3& actual, const glm::vec3& expected) {
        constexpr float tolerance = 0.0001f;
        for (int axis = 0; axis < 3; ++axis) {
            if (!std::isfinite(actual[axis]) || std::abs(actual[axis] - expected[axis]) > tolerance)
                return false;
        }
        return true;
    };
    const auto printBounds = [](std::string_view label, const learning::WorldBounds& bounds) {
        std::cout << label << " center=(" << bounds.center.x << ", " << bounds.center.y
                  << ", " << bounds.center.z << "), halfExtents=(" << bounds.halfExtents.x
                  << ", " << bounds.halfExtents.y << ", " << bounds.halfExtents.z << ")\n";
    };
    const auto check = [&](std::string_view name, const glm::vec3& localCenter,
                           const glm::vec3& localHalfExtents, const glm::mat4& transform,
                           const learning::WorldBounds& expected) {
        ++checks;
        const auto actual = learning::computeWorldBounds(localCenter, localHalfExtents, transform);
        if (near(actual.center, expected.center) && near(actual.halfExtents, expected.halfExtents)) {
            std::cout << "PASS: " << name << '\n';
        } else {
            ++failures;
            std::cout << "FAIL: " << name << '\n';
            printBounds("  expected", expected);
            printBounds("  actual  ", actual);
        }
    };

    check("identity at origin", {0.f, 0.f, 0.f}, {2.f, 1.f, 0.5f}, identity,
          {{0.f, 0.f, 0.f}, {2.f, 1.f, 0.5f}});
    check("identity preserves an offset local center", {3.f, -2.f, 5.f}, {0.5f, 1.f, 2.f}, identity,
          {{3.f, -2.f, 5.f}, {0.5f, 1.f, 2.f}});

    // These boxes are wholly away from zero: initializing extrema to zero is incorrect.
    check("translation wholly into positive coordinates", {0.f, 0.f, 0.f}, {1.f, 1.f, 1.f},
          glm::translate(identity, {10.f, 3.f, 5.f}),
          {{10.f, 3.f, 5.f}, {1.f, 1.f, 1.f}});
    check("translation wholly into negative coordinates", {0.f, 0.f, 0.f}, {1.f, 1.f, 1.f},
          glm::translate(identity, {-10.f, -3.f, -5.f}),
          {{-10.f, -3.f, -5.f}, {1.f, 1.f, 1.f}});

    check("nonuniform scale transforms center and sizes", {1.f, 2.f, -1.f}, {2.f, 1.f, 0.5f},
          glm::scale(identity, {2.f, 3.f, 4.f}),
          {{2.f, 6.f, -4.f}, {4.f, 3.f, 2.f}});
    check("negative scale keeps half-extents nonnegative", {1.f, 2.f, -1.f}, {2.f, 1.f, 0.5f},
          glm::scale(identity, {-2.f, 3.f, -4.f}),
          {{-2.f, 6.f, 4.f}, {4.f, 3.f, 2.f}});

    const auto turn90 = glm::rotate(identity, glm::radians(90.f), glm::vec3(0.f, 1.f, 0.f));
    check("90-degree Y rotation exchanges X and Z half-extents", {0.f, 0.f, 0.f}, {2.f, 1.f, 0.5f}, turn90,
          {{0.f, 0.f, 0.f}, {0.5f, 1.f, 2.f}});
    check("45-degree Y rotation encloses every corner", {0.f, 0.f, 0.f}, {2.f, 1.f, 0.5f},
          glm::rotate(identity, glm::radians(45.f), glm::vec3(0.f, 1.f, 0.f)),
          {{0.f, 0.f, 0.f}, {1.76776695f, 1.f, 1.76776695f}});

    const auto parentWorld = glm::translate(identity, {10.f, 0.f, 0.f}) * turn90;
    const auto childLocal = glm::translate(identity, {0.f, 0.f, 3.f});
    check("composed parent and child transforms", {1.f, 0.f, 0.f}, {2.f, 1.f, 0.5f},
          parentWorld * childLocal, {{13.f, 0.f, -1.f}, {0.5f, 1.f, 2.f}});

    glm::mat4 shear(1.f);
    shear[1][0] = 2.f; // X becomes X + 2*Y; Y and Z are unchanged.
    check("affine shear includes cross-axis reach", {1.f, 2.f, 0.f}, {1.f, 3.f, 0.5f}, shear,
          {{5.f, 2.f, 0.f}, {7.f, 3.f, 0.5f}});
    check("zero scale can collapse one axis", {1.f, 2.f, 3.f}, {2.f, 1.f, 0.5f},
          glm::translate(identity, {4.f, 5.f, 6.f}) * glm::scale(identity, {0.f, 2.f, 1.f}),
          {{4.f, 9.f, 9.f}, {0.f, 2.f, 0.5f}});
    check("zero-size box remains a transformed point", {1.f, 2.f, 3.f}, {0.f, 0.f, 0.f},
          glm::translate(identity, {4.f, -2.f, 1.f}) * glm::scale(identity, {2.f, 3.f, 4.f}),
          {{6.f, 4.f, 13.f}, {0.f, 0.f, 0.f}});

    std::cout << '\n' << checks - failures << '/' << checks << " checks passed.\n";
    return failures == 0 ? 0 : 1;
}
} // namespace

int main()
{
    const int frustumResult = runFrustumChecks();
    const int boundsResult = runWorldBoundsChecks();
    return frustumResult == 0 && boundsResult == 0 ? 0 : 1;
}
