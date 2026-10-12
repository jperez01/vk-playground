// Records GPU uploads (meshes, textures) into the upload context's command buffer
// and submits them together with a single wait. Staging buffers live until the submit completes.

#pragma once

#include <vk_types.h>

#include <span>
#include <vector>

// Vulkan objects an UploadBatch records and submits with. Owned by VulkanEngine.
struct UploadContext {
    VkDevice device { VK_NULL_HANDLE };
    VmaAllocator allocator { VK_NULL_HANDLE };
    VkQueue queue { VK_NULL_HANDLE };
    VkCommandPool commandPool { VK_NULL_HANDLE };
    VkCommandBuffer commandBuffer { VK_NULL_HANDLE };
    VkFence fence { VK_NULL_HANDLE };
    bool batchOpen { false };
};

class UploadBatch {
public:
    explicit UploadBatch(UploadContext& context);
    ~UploadBatch();
    UploadBatch(const UploadBatch&) = delete;
    UploadBatch& operator=(const UploadBatch&) = delete;

    GPUMeshBuffers uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices);
    AllocatedImage uploadImage(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped);
    void submitAndWait();

private:
    UploadContext& context;
    VkCommandBuffer cmd;
    std::vector<AllocatedBuffer> stagingBuffers;
    bool submitted { false };
};
