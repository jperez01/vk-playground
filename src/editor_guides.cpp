#include "editor_guides.h"

#include <vk_initializers.h>
#include <vk_pipelines.h>
#include <stdexcept>

namespace {
struct GuidePushConstants {
    glm::mat4 inverseViewProjection;
    glm::vec4 viewportMode; // width, height, mode (0 = grid, 1 = axes), unused
};
static_assert(sizeof(GuidePushConstants) == 80);
}

void EditorGuides::init(VkDevice device, VkDescriptorSetLayout sceneLayout, VkFormat colorFormat, VkFormat depthFormat)
{
    VkShaderModule vertex = VK_NULL_HANDLE;
    VkShaderModule fragment = VK_NULL_HANDLE;
    try {
        if (!vkutil::loadShaderModule("editor_grid.vert", device, &vertex) ||
            !vkutil::loadShaderModule("editor_grid.frag", device, &fragment)) {
            throw std::runtime_error("Failed to load editor guide shaders");
        }
        VkPushConstantRange range{};
        range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        range.size = sizeof(GuidePushConstants);
        auto info = vkinit::pipelineLayoutCreateInfo();
        info.setLayoutCount = 1;
        info.pSetLayouts = &sceneLayout;
        info.pushConstantRangeCount = 1;
        info.pPushConstantRanges = &range;
        VK_CHECK(vkCreatePipelineLayout(device, &info, nullptr, &layout));

        PipelineBuilder builder;
        builder.setShaders(vertex, fragment);
        builder.setInputTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        builder.setPolygonMode(VK_POLYGON_MODE_FILL);
        builder.setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
        builder.setMultisamplingNone();
        builder.enableBlendingAlphaBlend();
        builder.enableDepthTest(false, VK_COMPARE_OP_GREATER_OR_EQUAL);
        builder.setColorAttachmentFormat(colorFormat);
        builder.setDepthFormat(depthFormat);
        builder.pipelineLayout = layout;
        pipeline = builder.buildPipeline(device);
        if (pipeline == VK_NULL_HANDLE) throw std::runtime_error("Failed to create editor guide pipeline");
    } catch (...) {
        vkDestroyShaderModule(device, vertex, nullptr);
        vkDestroyShaderModule(device, fragment, nullptr);
        cleanup(device);
        throw;
    }
    vkDestroyShaderModule(device, vertex, nullptr);
    vkDestroyShaderModule(device, fragment, nullptr);
}

void EditorGuides::draw(VkCommandBuffer cmd, VkDescriptorSet sceneDescriptor,
    const glm::mat4& viewProjection, VkExtent2D extent) const
{
    if (!showGrid && !showAxes) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &sceneDescriptor, 0, nullptr);
    const VkViewport viewport{0.f, 0.f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.f, 1.f};
    const VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdSetCullMode(cmd, VK_CULL_MODE_NONE);
    vkCmdSetFrontFace(cmd, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    vkCmdSetDepthTestEnable(cmd, VK_TRUE);
    vkCmdSetDepthWriteEnable(cmd, VK_FALSE);
    vkCmdSetDepthCompareOp(cmd, VK_COMPARE_OP_GREATER_OR_EQUAL);

    GuidePushConstants push{glm::inverse(viewProjection),
        glm::vec4(static_cast<float>(extent.width), static_cast<float>(extent.height), 0.f, 0.f)};
    const auto stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    if (showGrid) {
        vkCmdPushConstants(cmd, layout, stages, 0, sizeof(push), &push);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }
    if (showAxes) {
        push.viewportMode.z = 1.f;
        vkCmdPushConstants(cmd, layout, stages, 0, sizeof(push), &push);
        vkCmdDraw(cmd, 18, 1, 0, 0);
    }
}

void EditorGuides::cleanup(VkDevice device)
{
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyPipelineLayout(device, layout, nullptr);
    pipeline = VK_NULL_HANDLE;
    layout = VK_NULL_HANDLE;
}
