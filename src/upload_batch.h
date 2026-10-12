// Records GPU uploads (meshes, textures) into the engine's immediate command buffer
// and submits them together with a single wait. Staging buffers live until the submit completes.

#pragma once

#include <vk_types.h>

#include <span>
#include <vector>

class VulkanEngine;

class UploadBatch {
public:
    explicit UploadBatch(VulkanEngine& engine);
    ~UploadBatch();
    UploadBatch(const UploadBatch&) = delete;
    UploadBatch& operator=(const UploadBatch&) = delete;

    GPUMeshBuffers uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices);
    AllocatedImage uploadImage(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped);
    void submitAndWait();

private:
    VulkanEngine& engine;
    VkCommandBuffer cmd;
    std::vector<AllocatedBuffer> stagingBuffers;
    bool submitted { false };
};
