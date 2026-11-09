#include "transform_gizmo.h"

#include "scene.h"

#include <ImGuizmo.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/matrix.hpp>

#include <cmath>

namespace Editor {
namespace {

ImGuizmo::OPERATION gizmoOperation(TransformOperation operation)
{
    switch (operation) {
    case TransformOperation::Translate:
        return ImGuizmo::TRANSLATE;
    case TransformOperation::Rotate:
        return ImGuizmo::ROTATE;
    case TransformOperation::Scale:
        return ImGuizmo::SCALE;
    }
    return ImGuizmo::TRANSLATE;
}

bool isFinite(const glm::mat4& matrix)
{
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (!std::isfinite(matrix[column][row])) {
                return false;
            }
        }
    }
    return true;
}

}

bool TransformGizmo::draw(Node* node, const glm::mat4& view,
    const glm::mat4& projection, ImVec2 renderPosition, ImVec2 renderSize,
    ImVec2 clipPosition, ImVec2 clipSize, bool pointerAvailable)
{
    ImGuizmo::BeginFrame();
    ImGuizmo::PushID(this);

    if (selectedNode_ != node || previousOperation_ != operation
        || previousLocalSpace_ != localSpace) {
        // The wrapper ID stays stable so disabling also cancels the old selection.
        ImGuizmo::Enable(false);
        using_ = false;
    }
    selectedNode_ = node;
    previousOperation_ = operation;
    previousLocalSpace_ = localSpace;
    capturesMouse_ = false;

    if (!node || renderSize.x <= 0.0f || renderSize.y <= 0.0f
        || clipSize.x <= 0.0f || clipSize.y <= 0.0f) {
        ImGuizmo::Enable(false);
        using_ = false;
        ImGuizmo::Enable(true);
        ImGuizmo::PopID();
        return false;
    }

    const ImVec2 clipEnd(clipPosition.x + clipSize.x, clipPosition.y + clipSize.y);
    const bool insideContent = ImGui::IsMouseHoveringRect(clipPosition, clipEnd, false);
    const bool popupOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup);
    const bool canBegin = pointerAvailable && insideContent && !popupOpen
        && !ImGui::IsAnyItemActive() && !ImGui::IsAnyItemHovered();
    const bool ownsDrag = using_ && !popupOpen;

    glm::mat4 parentWorld(1.0f);
    bool hasParent = false;
    if (const auto parent = node->parent.lock()) {
        parentWorld = parent->worldTransform;
        hasParent = true;
    }
    // Double precision avoids classifying small but invertible scene scales as
    // singular merely because their float determinant underflows. No epsilon cutoff.
    const double parentDeterminant = hasParent
        ? glm::determinant(glm::dmat4(parentWorld)) : 1.0;
    const bool invertibleParent = std::isfinite(parentDeterminant)
        && parentDeterminant != 0.0;
    if (!invertibleParent) {
        ImGuizmo::Enable(false);
        using_ = false;
        ImGuizmo::Enable(true);
        ImGuizmo::PopID();
        if (insideContent && pointerAvailable && !popupOpen) {
            ImGui::SetTooltip("Transform gizmo disabled: the parent world transform is singular or non-finite.\nRestore a non-zero parent scale before editing this node.");
        }
        return false;
    }

    // ImGui's background list is still above the rendered scene, but below panes
    // and the orientation widget, so object handles cannot cover navigation UI.
    ImDrawList* drawList = ImGui::GetBackgroundDrawList(ImGui::GetMainViewport());
    ImGuizmo::SetDrawlist(drawList);
    ImGuizmo::SetRect(renderPosition.x, renderPosition.y, renderSize.x, renderSize.y);
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::Enable(canBegin || ownsDrag);

    const auto nativeOperation = gizmoOperation(operation);
    const auto mode = (localSpace || operation == TransformOperation::Scale)
        ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
    glm::mat4 manipulatedWorld = node->worldTransform;
    drawList->PushClipRect(clipPosition, clipEnd, true);
    const bool manipulated = ImGuizmo::Manipulate(glm::value_ptr(view),
        glm::value_ptr(projection), nativeOperation, mode, glm::value_ptr(manipulatedWorld));
    drawList->PopClipRect();

    using_ = ImGuizmo::IsUsing();
    capturesMouse_ = using_ || (canBegin && ImGuizmo::IsOver(nativeOperation));
    ImGuizmo::Enable(true);
    ImGuizmo::PopID();

    if (!manipulated || manipulatedWorld == node->worldTransform) {
        return false;
    }

    // Keep the complete affine matrix, including any imported shear. Rebuilding
    // from decomposed TRS values would silently discard information.
    const glm::mat4 localTransform = hasParent
        ? glm::mat4(glm::inverse(glm::dmat4(parentWorld)) * glm::dmat4(manipulatedWorld))
        : manipulatedWorld;
    if (!isFinite(localTransform)) {
        if (insideContent && !popupOpen) {
            ImGui::SetTooltip("Transform could not be represented as a finite local matrix.");
        }
        return false;
    }
    node->localTransform = localTransform;
    node->refreshTransform(parentWorld);
    return true;
}

bool TransformGizmo::capturesMouse() const
{
    return capturesMouse_;
}

bool TransformGizmo::isUsing() const
{
    return using_;
}

}
