#pragma once

#include <filesystem>

namespace vkutil {
// Resource paths are rooted at the executable, never the launch working directory.
std::filesystem::path runtimePath(const std::filesystem::path& relativePath);
}
