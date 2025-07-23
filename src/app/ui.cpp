//
// Created by jperez01 on 23/07/2025.
//

#include "ui.h"

#include "imgui.h"

void Editor::handleUi()
{
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New", "Ctrl+N")) {

            }
            if (ImGui::MenuItem("Load", "Ctrl+L")) {

            }
            if (ImGui::MenuItem("Save", "Ctrl+S")) {

            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Add")) {
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}
