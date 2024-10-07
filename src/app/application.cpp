//
// Created by jpabl on 05/10/2024.
//

#include "application.h"

#include <imgui_impl_sdl2.h>
#include <imgui_impl_vulkan.h>
#include <SDL.h>

Application::Application() {
	engine.init();
	isInitialized = true;
}

void Application::run() {
    SDL_Event e;
    bool bQuit = false;

    // main loop
    while (!bQuit) {
        // Handle events on queue
        while (SDL_PollEvent(&e) != 0) {
            // close the window when user alt-f4s or clicks the X button
            if (e.type == SDL_QUIT)
                bQuit = true;

            if (e.type == SDL_WINDOWEVENT) {

				if (e.window.event == SDL_WINDOWEVENT_RESIZED) {
                    engine.resize_requested = true;
				}
				if (e.window.event == SDL_WINDOWEVENT_MINIMIZED) {
					freeze_rendering = true;
				}
				if (e.window.event == SDL_WINDOWEVENT_RESTORED) {
					freeze_rendering = false;
				}
            }

            engine.handleSDLEvent(e);
        	ImGui_ImplSDL2_ProcessEvent(&e);
        }

        if (freeze_rendering) continue;

    	engine.run();
    }
}

void Application::cleanup() {
	if (isInitialized) {
		engine.cleanup();
	}
}
