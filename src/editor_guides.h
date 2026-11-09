#pragma once

#include <vk_types.h>

// Editor-only guides. No scene nodes, mesh buffers, or per-frame allocations.
class EditorGuides {
public:
    bool showGrid = true;
    bool showAxes = true;

    void init(VkDevice device, VkDescriptorSetLayout sceneLayout, VkFormat colorFormat, VkFormat depthFormat);
    void draw(VkCommandBuffer cmd, VkDescriptorSet sceneDescriptor, const glm::mat4& viewProjection, VkExtent2D extent) const;
    void cleanup(VkDevice device);

private:
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
};
