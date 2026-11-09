//
// Created by jperez01 on 23/07/2025.
//

#include "ui.h"
#include "orientation_widget.h"
#include "transform_gizmo.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "vk_engine.h"
#include "vk_loader.h"
#include "nfd.h"
#include "fmt/std.h"

#include <algorithm>
#include <filesystem>
#include <unordered_map>
#include <vector>

std::optional<std::string> openFileDialog()
{
    nfdopendialogu8args_t args = {};
    nfdu8char_t *outPath;
    nfdresult_t result = NFD_OpenDialogU8_With(&outPath, &args);

    if (result == NFD_OKAY) {
        std::string path(outPath);
        NFD_FreePath(outPath);
        return path;
    }
    if (result == NFD_CANCEL) {
        fmt::println("User cancelled the file search.");
    } else {
        fmt::println("Failed to open dialogue with error {}", NFD_GetError());
    }

    return std::nullopt;
}

namespace {
struct FileEntry {
    std::string path;
    std::string name;
    std::string format;
    std::weak_ptr<LoadedScene> scene;
    std::unordered_map<const Node*, std::string> nodeNames;
};

struct EditorState {
    bool sceneOpen = true;
    bool filesOpen = true;
    bool statsOpen = true;
    bool backgroundOpen = true;
    bool resetLayout = false;
    bool focusPrimaryPanes = false;
    std::vector<FileEntry> files;
    std::string selectedFile;
    std::weak_ptr<Node> selectedNode;
    Editor::TransformGizmo transformGizmo;
};

EditorState state;

// Rebuild presentation data only when a scene is added, removed, or replaced.
void syncFiles(const VulkanEngine& engine)
{
    bool changed = state.files.size() != engine.loadedScenes.size();
    for (const auto& file : state.files) {
        const auto it = engine.loadedScenes.find(file.path);
        if (it == engine.loadedScenes.end() || file.scene.lock() != it->second) {
            changed = true;
            break;
        }
    }
    if (!changed) return;

    state.selectedNode.reset();
    state.files.clear();
    for (const auto& [path, scene] : engine.loadedScenes) {
        const std::filesystem::path source(path);
        FileEntry file{path, source.filename().string(), source.extension().string(), scene, {}};
        for (const auto& [name, node] : scene->nodes) {
            file.nodeNames.try_emplace(node.get(), name);
        }
        state.files.push_back(std::move(file));
    }
    std::sort(state.files.begin(), state.files.end(), [](const FileEntry& a, const FileEntry& b) {
        return a.path < b.path;
    });
    const auto selected = std::find_if(state.files.begin(), state.files.end(), [](const FileEntry& file) {
        return file.path == state.selectedFile;
    });
    if (selected == state.files.end()) {
        state.selectedFile = state.files.empty() ? std::string{} : state.files.front().path;
    }
}

ImGuiID drawDockspace()
{
    const ImGuiID id = ImGui::GetID("EditorDockspace");
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (state.resetLayout || !ImGui::DockBuilderGetNode(id)) {
        ImGui::DockBuilderRemoveNode(id);
        ImGui::DockBuilderAddNode(id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(id, viewport->WorkSize);
        ImGuiID center = id;
        const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.22f, nullptr, &center);
        const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.30f, nullptr, &center);
        ImGui::DockBuilderDockWindow("Scene", left);
        ImGui::DockBuilderDockWindow("Files / Inspector", right);
        ImGui::DockBuilderDockWindow("Stats", left);
        ImGui::DockBuilderDockWindow("background", right);
        ImGui::DockBuilderFinish(id);
        state.focusPrimaryPanes = true;
        if (state.resetLayout) {
            state.sceneOpen = state.filesOpen = state.statsOpen = state.backgroundOpen = true;
        }
        state.resetLayout = false;
    }
    ImGui::DockSpaceOverViewport(id, viewport, ImGuiDockNodeFlags_PassthruCentralNode);
    return id;
}

const char* nodeName(const FileEntry& file, const Node& node)
{
    const auto it = file.nodeNames.find(&node);
    return it == file.nodeNames.end() || it->second.empty() ? "(unnamed node)" : it->second.c_str();
}

void drawNode(const FileEntry& file, const std::shared_ptr<Node>& node)
{
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (node->children.empty()) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (state.selectedFile == file.path && state.selectedNode.lock() == node) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    const bool open = ImGui::TreeNodeEx(node.get(), flags, "%s", nodeName(file, *node));
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        state.selectedFile = file.path;
        state.selectedNode = node;
        state.filesOpen = true;
        state.focusPrimaryPanes = true;
    }
    if (open && !node->children.empty()) {
        for (const auto& child : node->children) drawNode(file, child);
        ImGui::TreePop();
    }
}

void drawScene()
{
    if (!state.sceneOpen) return;
    if (state.focusPrimaryPanes) ImGui::SetNextWindowFocus();
    if (ImGui::Begin("Scene", &state.sceneOpen)) {
        if (state.files.empty()) ImGui::TextDisabled("No scenes loaded. Use File > Load.");
        for (const auto& file : state.files) {
            const auto scene = file.scene.lock();
            if (!scene) continue;
            ImGui::PushID(file.path.c_str());
            const bool open = ImGui::TreeNodeEx("scene", ImGuiTreeNodeFlags_DefaultOpen |
                ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth, "%s", file.name.c_str());
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                state.selectedFile = file.path;
                state.selectedNode.reset();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", file.path.c_str());
            if (open) {
                for (const auto& node : scene->topNodes) drawNode(file, node);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }
    ImGui::End();
}

void drawMatrix(const char* label, const glm::mat4& matrix)
{
    if (ImGui::TreeNode(label)) {
        for (int row = 0; row < 4; ++row) {
            ImGui::Text("% .3f  % .3f  % .3f  % .3f",
                matrix[0][row], matrix[1][row], matrix[2][row], matrix[3][row]);
        }
        ImGui::TreePop();
    }
}

void drawFiles()
{
    if (!state.filesOpen) return;
    if (state.focusPrimaryPanes) ImGui::SetNextWindowFocus();
    if (ImGui::Begin("Files / Inspector", &state.filesOpen)) {
        ImGui::SeparatorText("Loaded files");
        const float listHeight = std::clamp(ImGui::GetContentRegionAvail().y * 0.30f, 60.0f, 180.0f);
        if (ImGui::BeginChild("FileList", ImVec2(0, listHeight), ImGuiChildFlags_Borders)) {
            for (const auto& file : state.files) {
                ImGui::PushID(file.path.c_str());
                if (ImGui::Selectable(file.name.c_str(), state.selectedFile == file.path)) {
                    state.selectedFile = file.path;
                    state.selectedNode.reset();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", file.path.c_str());
                ImGui::PopID();
            }
            if (state.files.empty()) ImGui::TextDisabled("Use File > Load to import a model.");
        }
        ImGui::EndChild();
        for (const auto& file : state.files) {
            if (file.path != state.selectedFile) continue;
            const auto scene = file.scene.lock();
            if (!scene) break;
            ImGui::SeparatorText("Source file");
            ImGui::TextWrapped("%s", file.path.c_str());
            ImGui::Text("Format: %s", file.format.empty() ? "unknown" : file.format.c_str());
            ImGui::Text("Nodes: %zu", scene->nodes.size());
            ImGui::Text("Meshes: %zu", scene->meshes.size());
            ImGui::Text("Materials: %zu", scene->materials.size());
            ImGui::Text("Textures: %zu", scene->images.size());
            if (const auto node = state.selectedNode.lock()) {
                ImGui::SeparatorText("Selected node");
                ImGui::TextWrapped("%s", nodeName(file, *node));
                ImGui::Text("Children: %zu", node->children.size());
                drawMatrix("Local transform", node->localTransform);
                drawMatrix("World transform", node->worldTransform);
                if (const auto meshNode = std::dynamic_pointer_cast<MeshNode>(node); meshNode && meshNode->mesh) {
                    const auto& mesh = *meshNode->mesh;
                    ImGui::TextWrapped("Mesh: %s", mesh.name.c_str());
                    ImGui::Text("Surfaces: %zu", mesh.surfaces.size());
                    uint64_t indices = 0;
                    for (const auto& surface : mesh.surfaces) indices += surface.count;
                    ImGui::Text("Triangles: %llu", static_cast<unsigned long long>(indices / 3));
                }
            }
            break;
        }
    }
    ImGui::End();
}

void drawStats(VulkanEngine& engine)
{
    if (!state.statsOpen) return;
    if (ImGui::Begin("Stats", &state.statsOpen, ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::Text("frametime %f ms", engine.stats.frametime);
        ImGui::Text("geometry record (CPU) %f ms", engine.stats.geometryRecordTime);
        if (engine.gpuProfiler.supported()) {
            ImGui::SeparatorText("GPU (ms)");
            ImGui::Text("total %.3f", engine.gpuProfiler.totalMs());
            for (uint32_t i = 0; i < static_cast<uint32_t>(GpuPass::Count); ++i) {
                const auto pass = static_cast<GpuPass>(i);
                ImGui::Text("%s %.3f", GpuProfiler::passName(pass), engine.gpuProfiler.passMs(pass));
            }
        } else {
            ImGui::Text("GPU timestamps not supported");
        }
        const char* names[] = {"FIFO (vsync)", "MAILBOX", "IMMEDIATE"};
        const VkPresentModeKHR modes[] = {VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR};
        int current = 0;
        for (int i = 0; i < 3; ++i) if (modes[i] == engine.presentMode) current = i;
        if (ImGui::Combo("Present mode", &current, names, 3)) {
            engine.presentMode = modes[current];
            engine.resizeRequested = true;
        }
        ImGui::Text("triangles %i", engine.stats.triangleCount);
        ImGui::Text("draws %i", engine.stats.drawcallCount);
    }
    ImGui::End();
}

void drawBackground(VulkanEngine& engine)
{
    if (!state.backgroundOpen) return;
    if (ImGui::Begin("background", &state.backgroundOpen, ImGuiWindowFlags_NoFocusOnAppearing)) {
        if (!engine.backgroundEffects.empty()) {
            ImGui::SliderInt("Effect Index", &engine.currentBackgroundEffect, 0,
                static_cast<int>(engine.backgroundEffects.size()) - 1);
            auto& selected = engine.backgroundEffects[engine.currentBackgroundEffect];
            ImGui::Text("Selected effect: %s", selected.name);
            ImGui::InputFloat4("data1", &selected.data.data1.x);
            ImGui::InputFloat4("data2", &selected.data.data2.x);
            ImGui::InputFloat4("data3", &selected.data.data3.x);
            ImGui::InputFloat4("data4", &selected.data.data4.x);
        }
        ImGui::SliderFloat4("Sunlight Color", &engine.sceneData.sunlightColor.x, 0.0f, 1.0f);
        ImGui::SliderFloat4("Sunlight Direction", &engine.sceneData.sunlightDirection.x, -1.0f, 1.0f);
    }
    ImGui::End();
}
} // namespace

void Editor::handleUi(VulkanEngine* engine, Camera& camera)
{
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Load")) {
                if (auto path = openFileDialog()) {
                    const auto source = std::filesystem::absolute(*path).lexically_normal().string();
                    if (auto scene = loadModel(engine, source)) {
                        engine->loadedScenes[source] = *scene;
                        state.selectedFile = source;
                        state.selectedNode.reset();
                    } else {
                        fmt::println("Failed to load model {}", source);
                    }
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Scene", nullptr, &state.sceneOpen);
            ImGui::MenuItem("Files / Inspector", nullptr, &state.filesOpen);
            ImGui::MenuItem("Stats", nullptr, &state.statsOpen);
            ImGui::MenuItem("background", nullptr, &state.backgroundOpen);
            ImGui::Separator();
            ImGui::MenuItem("Grid", nullptr, &engine->editorGuides.showGrid);
            ImGui::MenuItem("Axes", nullptr, &engine->editorGuides.showAxes);
            if (ImGui::MenuItem("Reset Layout")) state.resetLayout = true;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Camera")) {
            ImGui::TextDisabled("WASD movement speed");
            ImGui::SetNextItemWidth(220.f);
            ImGui::SliderFloat("Move speed", &camera.movementSpeed, 0.1f, 20.f,
                "%.1f units/s", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ctrl+click to enter a speed.");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Gizmo")) {
            auto& gizmo = state.transformGizmo;
            if (ImGui::MenuItem("Translate", nullptr, gizmo.operation == Editor::TransformOperation::Translate))
                gizmo.operation = Editor::TransformOperation::Translate;
            if (ImGui::MenuItem("Rotate", nullptr, gizmo.operation == Editor::TransformOperation::Rotate))
                gizmo.operation = Editor::TransformOperation::Rotate;
            if (ImGui::MenuItem("Scale", nullptr, gizmo.operation == Editor::TransformOperation::Scale))
                gizmo.operation = Editor::TransformOperation::Scale;
            ImGui::Separator();
            ImGui::MenuItem("Local axes", nullptr, &gizmo.localSpace);
            ImGui::TextDisabled("Scaling always uses local axes.");
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
    const ImGuiID dockspaceId = drawDockspace();
    syncFiles(*engine);
    drawScene();
    drawFiles();
    drawStats(*engine);
    drawBackground(*engine);

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImGuiDockNode* center = ImGui::DockBuilderGetCentralNode(dockspaceId);
    const ImVec2 clipPosition = center ? center->Pos : viewport->WorkPos;
    const ImVec2 clipSize = center ? center->Size : viewport->WorkSize;
    const auto selected = state.selectedNode.lock();
    const glm::vec3 pivot = selected ? glm::vec3(selected->worldTransform[3]) : glm::vec3(0.f);
    Editor::drawOrientationWidget(camera, clipPosition, clipSize, pivot,
        !state.transformGizmo.isUsing());

    // Undo Vulkan's Y flip and reversed depth for ImGuizmo's screen convention.
    glm::mat4 gizmoProjection = engine->sceneData.proj;
    gizmoProjection[1][1] *= -1.f;
    for (int column = 0; column < 4; ++column)
        gizmoProjection[column][2] = gizmoProjection[column][3] - gizmoProjection[column][2];
    state.transformGizmo.draw(selected.get(), camera.getViewMatrix(), gizmoProjection,
        viewport->Pos, viewport->Size, clipPosition, clipSize,
        !Editor::orientationWidgetCapturesMouse());
    state.focusPrimaryPanes = false;
}

bool Editor::capturesPointer()
{
    return state.transformGizmo.capturesMouse() || orientationWidgetCapturesMouse();
}

void Editor::setupTheme()
{
     ImGuiStyle& style  = ImGui::GetStyle();
    ImVec4*     colors = style.Colors;

    // Corners
    style.WindowRounding    = 8.0f;
    style.ChildRounding     = 8.0f;
    style.FrameRounding     = 6.0f;
    style.PopupRounding     = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding      = 6.0f;
    style.TabRounding       = 6.0f;

    // Colors
    colors[ImGuiCol_Text]                  = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
    colors[ImGuiCol_TextDisabled]          = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_WindowBg]              = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    colors[ImGuiCol_ChildBg]               = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    colors[ImGuiCol_PopupBg]               = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
    colors[ImGuiCol_Border]                = ImVec4(0.25f, 0.25f, 0.25f, 0.70f);
    colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]               = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_FrameBgActive]         = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    colors[ImGuiCol_TitleBg]               = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    colors[ImGuiCol_TitleBgActive]         = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    colors[ImGuiCol_MenuBarBg]             = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
    colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
    colors[ImGuiCol_CheckMark]             = ImVec4(0.80f, 0.80f, 0.80f, 1.00f);
    colors[ImGuiCol_SliderGrab]            = ImVec4(0.70f, 0.70f, 0.70f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);
    colors[ImGuiCol_Button]                = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_ButtonHovered]         = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    colors[ImGuiCol_ButtonActive]          = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
    colors[ImGuiCol_Header]                = ImVec4(0.25f, 0.25f, 0.25f, 0.55f);
    colors[ImGuiCol_HeaderHovered]         = ImVec4(0.35f, 0.35f, 0.35f, 0.80f);
    colors[ImGuiCol_HeaderActive]          = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
    colors[ImGuiCol_Separator]             = ImVec4(0.30f, 0.30f, 0.30f, 0.50f);
    colors[ImGuiCol_SeparatorHovered]      = ImVec4(0.45f, 0.45f, 0.45f, 0.78f);
    colors[ImGuiCol_SeparatorActive]       = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_ResizeGrip]            = ImVec4(0.30f, 0.30f, 0.30f, 0.25f);
    colors[ImGuiCol_ResizeGripHovered]     = ImVec4(0.45f, 0.45f, 0.45f, 0.67f);
    colors[ImGuiCol_ResizeGripActive]      = ImVec4(0.50f, 0.50f, 0.50f, 0.95f);
    colors[ImGuiCol_Tab]                   = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    colors[ImGuiCol_TabHovered]            = ImVec4(0.30f, 0.30f, 0.30f, 0.80f);
    colors[ImGuiCol_TabActive]             = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_TabUnfocused]          = ImVec4(0.10f, 0.10f, 0.10f, 0.97f);
    colors[ImGuiCol_TabUnfocusedActive]    = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    //colors[ImGuiCol_DockingPreview]        = ImVec4(0.30f, 0.30f, 0.30f, 0.70f);
    //colors[ImGuiCol_DockingEmptyBg]        = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    colors[ImGuiCol_PlotLines]             = ImVec4(0.70f, 0.70f, 0.70f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered]      = ImVec4(0.90f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_PlotHistogram]         = ImVec4(0.80f, 0.65f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered]  = ImVec4(0.90f, 0.50f, 0.00f, 1.00f);
    colors[ImGuiCol_TextSelectedBg]        = ImVec4(0.30f, 0.30f, 0.30f, 0.35f);
    colors[ImGuiCol_DragDropTarget]        = ImVec4(1.00f, 0.00f, 0.00f, 0.90f);
    colors[ImGuiCol_NavHighlight]          = ImVec4(0.70f, 0.70f, 0.70f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
}
