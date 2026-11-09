//
// Created by jpabl on 05/10/2024.
//

#include "application.h"
#include "ui.h"

#include <imgui_impl_sdl2.h>
#include <imgui_impl_vulkan.h>
#include <nfd.h>
#include <SDL.h>
#include <glm/trigonometric.hpp>
#include <algorithm>

Application::Application() {
	engine.init();

	mainCamera.velocity = glm::vec3(0.f);
    mainCamera.position = glm::vec3(4.f, 3.f, 4.f);
    mainCamera.pitch = -glm::atan(3.f / glm::sqrt(32.f));
    mainCamera.yaw = glm::radians(-45.f);

	isInitialized = true;
	NFD_Init();
}

void Application::run() {
    SDL_Event e;
    bool bQuit = false;
    const double counterFrequency = static_cast<double>(SDL_GetPerformanceFrequency());
    Uint64 previousCounter = SDL_GetPerformanceCounter();

    // main loop
    while (!bQuit) {
        const Uint64 counter = SDL_GetPerformanceCounter();
        const float deltaSeconds = std::min(
            static_cast<float>((counter - previousCounter) / counterFrequency), 0.1f);
        previousCounter = counter;
        // Handle events on queue
        while (SDL_PollEvent(&e) != 0) {
            // close the window when user alt-f4s or clicks the X button
            if (e.type == SDL_QUIT)
                bQuit = true;

            if (e.type == SDL_WINDOWEVENT) {

				if (e.window.event == SDL_WINDOWEVENT_RESIZED) {
                    engine.resizeRequested = true;
				}
				if (e.window.event == SDL_WINDOWEVENT_MINIMIZED) {
					freezeRendering = true;
				}
				if (e.window.event == SDL_WINDOWEVENT_RESTORED) {
					freezeRendering = false;
				}
            }

            ImGui_ImplSDL2_ProcessEvent(&e);
            const auto& io = ImGui::GetIO();
            const bool keyboard = e.type == SDL_KEYDOWN || e.type == SDL_KEYUP;
            const bool mouse = e.type == SDL_MOUSEMOTION || e.type == SDL_MOUSEBUTTONDOWN ||
                e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEWHEEL;
            const bool looking = e.type != SDL_MOUSEMOTION || (e.motion.state & SDL_BUTTON_RMASK) != 0;
            const bool gizmoCapture = Editor::capturesPointer();
            // Always deliver key releases, even when a widget takes keyboard focus.
            if (e.type == SDL_KEYUP || (!gizmoCapture && (!keyboard || !io.WantCaptureKeyboard) &&
                (!mouse || (!io.WantCaptureMouse && looking)))) {
                mainCamera.processSDLEvent(e);
            }
            if (gizmoCapture || io.WantCaptureKeyboard || (e.type == SDL_WINDOWEVENT &&
                e.window.event == SDL_WINDOWEVENT_FOCUS_LOST)) {
                mainCamera.velocity = glm::vec3(0.f);
            }
        }

        if (freezeRendering) continue;

        mainCamera.update(deltaSeconds);
        engine.run(mainCamera);
    }
}

void Application::cleanup() {
	if (isInitialized) {
		engine.cleanup();
	}
	NFD_Quit();
}
