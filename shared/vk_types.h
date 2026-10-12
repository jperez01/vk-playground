// Core GPU resource types (RAII buffers/images), vertex/material layouts and the scene node.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>
#include <vulkan/vk_enum_string_helper.h>
#include <vk_mem_alloc.h>

#include <fmt/core.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>



// forward declarations for RAII cleanup
namespace vkutil {
    void deferDestroyBuffer(VkBuffer buffer, VmaAllocation allocation);
    void deferDestroyImage(VkImage image, VkImageView view, VmaAllocation allocation);
}

// we will add our main reusable types here
struct AllocatedImage {
    VkImage image { VK_NULL_HANDLE };
    VkImageView imageView { VK_NULL_HANDLE };
    VmaAllocation allocation { VK_NULL_HANDLE };
    VkExtent3D imageExtent { 0, 0, 0 };
    VkFormat imageFormat { VK_FORMAT_UNDEFINED };

    AllocatedImage() = default;
    ~AllocatedImage() {
        if (image != VK_NULL_HANDLE || imageView != VK_NULL_HANDLE || allocation != VK_NULL_HANDLE) {
            vkutil::deferDestroyImage(image, imageView, allocation);
        }
    }

    AllocatedImage(const AllocatedImage&) = delete;
    AllocatedImage& operator=(const AllocatedImage&) = delete;

    AllocatedImage(AllocatedImage&& other) noexcept : image(other.image), imageView(other.imageView), allocation(other.allocation), imageExtent(other.imageExtent), imageFormat(other.imageFormat) {
        other.image = VK_NULL_HANDLE;
        other.imageView = VK_NULL_HANDLE;
        other.allocation = VK_NULL_HANDLE;
    }
    AllocatedImage& operator=(AllocatedImage&& other) noexcept {
        if (this != &other) {
            if (image != VK_NULL_HANDLE || imageView != VK_NULL_HANDLE || allocation != VK_NULL_HANDLE) {
                vkutil::deferDestroyImage(image, imageView, allocation);
            }
            image = other.image;
            imageView = other.imageView;
            allocation = other.allocation;
            imageExtent = other.imageExtent;
            imageFormat = other.imageFormat;
            
            other.image = VK_NULL_HANDLE;
            other.imageView = VK_NULL_HANDLE;
            other.allocation = VK_NULL_HANDLE;
        }
        return *this;
    }
};

struct AllocatedBuffer {
    VkBuffer buffer { VK_NULL_HANDLE };
    VmaAllocation allocation { VK_NULL_HANDLE };
    VmaAllocationInfo info {};

    AllocatedBuffer() = default;
    ~AllocatedBuffer() {
        if (buffer != VK_NULL_HANDLE || allocation != VK_NULL_HANDLE) {
            vkutil::deferDestroyBuffer(buffer, allocation);
        }
    }

    AllocatedBuffer(const AllocatedBuffer&) = delete;
    AllocatedBuffer& operator=(const AllocatedBuffer&) = delete;

    AllocatedBuffer(AllocatedBuffer&& other) noexcept : buffer(other.buffer), allocation(other.allocation), info(other.info) {
        other.buffer = VK_NULL_HANDLE;
        other.allocation = VK_NULL_HANDLE;
    }
    AllocatedBuffer& operator=(AllocatedBuffer&& other) noexcept {
        if (this != &other) {
            if (buffer != VK_NULL_HANDLE || allocation != VK_NULL_HANDLE) {
                vkutil::deferDestroyBuffer(buffer, allocation);
            }
            buffer = other.buffer;
            allocation = other.allocation;
            info = other.info;
            
            other.buffer = VK_NULL_HANDLE;
            other.allocation = VK_NULL_HANDLE;
        }
        return *this;
    }
};

struct GPUGLTFMaterial {
    glm::vec4 colorFactors;
    glm::vec4 metal_rough_factors;
    int colorTexID;
    int metalRoughTexID;
    int padding[2];
};


struct GPUSceneData {
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 viewproj;
    glm::vec4 ambientColor;
    glm::vec4 sunlightDirection = {1.0f, -0.5f, -1.0f, 0.0f}; // w for sun power
    glm::vec4 sunlightColor = {1.0f, 1.0f, 1.0f, 0.0f};
};

enum class MaterialPass :uint8_t {
    MainColor,
    Transparent,
    Other
};
struct MaterialPipeline {
	VkPipeline pipeline;
	VkPipelineLayout layout;
};
struct MaterialInstance {
    MaterialPipeline* pipeline;
    VkDeviceAddress materialBuffer;
    MaterialPass passType;
};
struct Vertex {

	glm::vec3 position;
	float uv_x;
	glm::vec3 normal;
	float uv_y;
	glm::vec4 color;
};

// holds the resources needed for a mesh
struct GPUMeshBuffers {

    AllocatedBuffer indexBuffer;
    AllocatedBuffer vertexBuffer;
    VkDeviceAddress vertexBufferAddress;
};

// push constants for our mesh object draws
struct GPUDrawPushConstants {
    glm::mat4 worldMatrix;
    VkDeviceAddress vertexBuffer;
    VkDeviceAddress materialBuffer;
};

struct DrawContext;

// base class for a renderable dynamic object
class IRenderable {

    virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx) = 0;
};

// implementation of a drawable scene node.
// the scene node can hold children and will also keep a transform to propagate
// to them
struct Node : public IRenderable {

    // parent pointer must be a weak pointer to avoid circular dependencies
    std::weak_ptr<Node> parent;
    std::vector<std::shared_ptr<Node>> children;

    glm::mat4 localTransform;
    glm::mat4 worldTransform;

    void refreshTransform(const glm::mat4& parentMatrix)
    {
        worldTransform = parentMatrix * localTransform;
        for (auto c : children) {
            c->refreshTransform(worldTransform);
        }
    }

    virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx)
    {
        // draw children
        for (auto& c : children) {
            c->Draw(topMatrix, ctx);
        }
    }
};

#define VK_CHECK(x)                                                     \
    do {                                                                \
        VkResult err = x;                                               \
        if (err) {                                                      \
             fmt::print("Detected Vulkan error: {}", string_VkResult(err)); \
            abort();                                                    \
        }                                                               \
    } while (0)
