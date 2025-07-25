//
// Created by jpabl on 05/10/2024.
//

#ifndef APPLICATION_H
#define APPLICATION_H
#include "vk_engine.h"


class Application {
public:
    Application();

    VulkanEngine engine;
    Camera mainCamera;
    bool isInitialized { false };
    bool resize_requested{false};
    bool freeze_rendering{false};

    void run();
    void cleanup();
};


#endif //APPLICATION_H
