#pragma once

#include <imgui.h>
#include <glm/vec3.hpp>

class Camera;

namespace Editor {
bool drawOrientationWidget(Camera& camera, ImVec2 clipPosition, ImVec2 clipSize,
                           const glm::vec3& pivot, bool pointerAvailable);
bool orientationWidgetCapturesMouse();
}
