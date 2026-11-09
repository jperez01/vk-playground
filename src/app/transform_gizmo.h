#pragma once

#include <glm/mat4x4.hpp>
#include <imgui.h>

struct Node;

namespace Editor {

enum class TransformOperation { Translate, Rotate, Scale };

class TransformGizmo {
public:
    TransformOperation operation = TransformOperation::Translate;
    bool localSpace = false;

    bool draw(Node* node, const glm::mat4& view, const glm::mat4& projection,
        ImVec2 renderPosition, ImVec2 renderSize, ImVec2 clipPosition,
        ImVec2 clipSize, bool pointerAvailable);
    bool capturesMouse() const;
    bool isUsing() const;

private:
    // Identity only: never dereferenced across frames or used to retain a scene.
    Node* selectedNode_ = nullptr;
    TransformOperation previousOperation_ = TransformOperation::Translate;
    bool previousLocalSpace_ = false;
    bool capturesMouse_ = false;
    bool using_ = false;
};

}
