#include "orientation_widget.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>

#define IMVIEWGUIZMO_IMPLEMENTATION
#include <ImViewGuizmo.h>

#include <camera.h>

namespace Editor {
namespace {
bool capturesMouse = false;
bool ownsPress = false;

bool validRotation(const glm::quat& rotation)
{
    const float normSquared = glm::dot(rotation, rotation);
    return std::isfinite(normSquared) && normSquared > 1.e-12f;
}

void releaseOwnership()
{
    auto& context = ImViewGuizmo::GetContext();
    context.activeTool = ImViewGuizmo::TOOL_NONE;
    context.pressedAxisID = -1;
    context.isAnimating = false;
    context.Reset();
    ownsPress = false;
    capturesMouse = false;
}
}

bool drawOrientationWidget(Camera& camera, ImVec2 clipPosition, ImVec2 clipSize,
                           const glm::vec3& pivot, bool pointerAvailable)
{
    ImViewGuizmo::BeginFrame();
    capturesMouse = false;

    constexpr float widgetSize = 96.f;
    constexpr float margin = 8.f;
    if (!std::isfinite(clipPosition.x) || !std::isfinite(clipPosition.y)
        || !std::isfinite(clipSize.x) || !std::isfinite(clipSize.y)
        || clipSize.x < widgetSize + 2.f * margin
        || clipSize.y < widgetSize + 2.f * margin) {
        releaseOwnership();
        return false;
    }

    const glm::quat initialRotation = glm::quat_cast(camera.getRotationMatrix());
    if (!validRotation(initialRotation)
        || !std::isfinite(camera.position.x) || !std::isfinite(camera.position.y)
        || !std::isfinite(camera.position.z) || !std::isfinite(pivot.x)
        || !std::isfinite(pivot.y) || !std::isfinite(pivot.z)) {
        releaseOwnership();
        return false;
    }

    const bool popupOpen = ImGui::IsPopupOpen(nullptr,
        ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    const bool inputAvailable = pointerAvailable && !popupOpen
        && (!ImGui::IsAnyItemActive() || ownsPress || ImViewGuizmo::IsUsing())
        && !(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NoMouse);
    if (!inputAvailable)
        releaseOwnership();

    const ImVec2 windowPosition(clipPosition.x + clipSize.x - widgetSize - margin,
                               clipPosition.y + margin);
    ImGui::SetNextWindowPos(windowPosition, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(widgetSize, widgetSize), ImGuiCond_Always);
    ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
    constexpr ImGuiWindowFlags baseFlags = ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDocking
        | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing
        | ImGuiWindowFlags_NoBringToFrontOnFocus
        | ImGuiWindowFlags_NoScrollWithMouse;
    const ImGuiWindowFlags flags = baseFlags
        | (inputAvailable ? 0 : ImGuiWindowFlags_NoMouseInputs);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    const bool visible = ImGui::Begin("##EditorOrientationWidget", nullptr, flags);
    bool changed = false;
    if (visible) {
        auto& context = ImViewGuizmo::GetContext();
        auto& io = ImGui::GetIO();
        const bool interacting = ownsPress || ImViewGuizmo::IsUsing()
            || context.isAnimating;
        const bool acceptsPointer = inputAvailable
            && (ImGui::IsWindowHovered() || interacting)
            // Upstream snaps on release, so a press must originate here.
            && (!ImGui::IsMouseReleased(ImGuiMouseButton_Left) || ownsPress);

        auto& style = ImViewGuizmo::GetStyle();
        style.scale = 0.55f;
        style.labelSize = 0.85f;
        // Upstream's linear snap animation normalizes zero for opposite axes
        // and has no target direction at zero orbit distance. Instant snaps
        // use its safe quatLookAt path, including the vertical-axis fallback.
        style.animateSnap = false;

        glm::vec3 position = camera.position;
        glm::quat rotation = glm::normalize(initialRotation);
        const bool zeroOrbitDistance = camera.position == pivot;
        const ImGuiConfigFlags previousConfigFlags = io.ConfigFlags;
        if (!acceptsPointer)
            io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
        ImGui::GetWindowDrawList()->PushClipRect(clipPosition,
            ImVec2(clipPosition.x + clipSize.x, clipPosition.y + clipSize.y), true);
        // Despite its API comment, Rotate's position is the gizmo center.
        const bool modified = ImViewGuizmo::Rotate(position, rotation, pivot,
            ImVec2(windowPosition.x + widgetSize * 0.5f,
                   windowPosition.y + widgetSize * 0.5f));
        ImGui::GetWindowDrawList()->PopClipRect();
        io.ConfigFlags = previousConfigFlags;
        // A zero-radius orbit may change orientation, never camera position.
        if (zeroOrbitDistance)
            position = pivot;

        if (acceptsPointer && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
            && ImViewGuizmo::IsOver())
            ownsPress = true;
        capturesMouse = inputAvailable && (ImViewGuizmo::IsOver()
            || ImViewGuizmo::IsUsing() || context.isAnimating || ownsPress);
        if (!io.MouseDown[ImGuiMouseButton_Left])
            ownsPress = false;

        if (modified && validRotation(rotation)) {
            const glm::vec3 forward = glm::normalize(
                glm::normalize(rotation) * glm::vec3(0.f, 0.f, -1.f));
            const float horizontalLength = std::hypot(forward.x, forward.z);
            const float pitch = std::atan2(forward.y, horizontalLength);
            // Yaw is undefined at the poles; retain the previous heading.
            const float yaw = horizontalLength > 1.e-5f
                ? std::atan2(forward.x, -forward.z) : camera.yaw;
            if (std::isfinite(position.x) && std::isfinite(position.y)
                && std::isfinite(position.z) && std::isfinite(pitch)
                && std::isfinite(yaw)) {
                changed = position != camera.position || pitch != camera.pitch || yaw != camera.yaw;
                camera.position = position;
                camera.pitch = pitch;
                camera.yaw = yaw;
            }
        }
    } else {
        releaseOwnership();
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    return changed;
}

bool orientationWidgetCapturesMouse()
{
    return capturesMouse;
}
}
