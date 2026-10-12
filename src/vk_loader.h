// glTF and Assimp scene loading into GPU meshes, materials and a node hierarchy.

#pragma once

#include "scene.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>

class VulkanEngine;

std::optional<std::shared_ptr<LoadedScene>> loadGltf(VulkanEngine* engine, std::string_view filePath);

// Imports a model with Assimp (OBJ, FBX, ...) and uploads it as a LoadedScene.
std::optional<std::shared_ptr<LoadedScene>> loadModel(VulkanEngine* engine, const std::string& filePath);
