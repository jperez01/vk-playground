//
// Created by jperez01 on 23/07/2025.
//

#ifndef UI_H
#define UI_H

#include <optional>
#include <string>

#include "vk_engine.h"

namespace Editor
{
    void handleUi(VulkanEngine* engine);
    void setupTheme();
}

std::optional<std::string> openFileDialog();


#endif //UI_H
