// VulkanEngine: owns the Vulkan device, swapchain, per-frame resources and the frame loop.

#pragma once

#include <vk_types.h>

#include <map>
#include <utility>
#include <string>
#include <unordered_map>
#include <vector>

#include <vk_mem_alloc.h>
 
#include <camera.h>
#include "scene.h"
#include "gpu_profiler.h"
#include "upload_batch.h"
#include <vk_deletion_queue.h>
#include <vk_descriptors.h>
#include <vk_pipelines.h>

struct ComputePushConstants {
	glm::vec4 data1;
	glm::vec4 data2;
	glm::vec4 data3;
	glm::vec4 data4;
};

struct ComputeEffect {
	const char* name;

	VkPipeline pipeline;
	VkPipelineLayout layout;

	ComputePushConstants data;
};

struct FrameData {
	VkSemaphore swapchainSemaphore;
	uint64_t frameTimelineValue {0};
    AllocatedBuffer sceneDataBuffer;
    VkDescriptorSet globalDescriptor;

    VkCommandPool commandPool;
    VkCommandBuffer mainCommandBuffer;
};

constexpr unsigned int FRAME_OVERLAP = 2;
constexpr uint32_t MAX_GLOBAL_TEXTURES = 1000;


struct EngineStats {
    float frametime;
    int triangleCount;
    int drawcallCount;
    float geometryRecordTime;
};

class VulkanEngine;

// Opaque and transparent mesh pipelines shared by glTF and Assimp scenes.
struct MeshPipelines {
    MaterialPipeline opaquePipeline;
    MaterialPipeline transparentPipeline;


    void buildPipelines(VulkanEngine* engine);
    void clearResources(VkDevice device);

};

class VulkanEngine {
public:
    int frameNumber { 0 };

    VkExtent2D windowExtent { 1280, 720 };

    struct SDL_Window* window { nullptr };

    VkInstance instance;
    VkDebugUtilsMessengerEXT debug_messenger;
    VkPhysicalDevice chosenGPU;
    VkDevice device;

    VkQueue graphicsQueue;
    uint32_t graphicsQueueFamily;

    VkDescriptorPool imguiPool {VK_NULL_HANDLE};

    FrameData frames[FRAME_OVERLAP];

    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    VkFormat swapchainImageFormat;
	VkExtent2D swapchainExtent;
	VkExtent2D drawExtent;


    DescriptorAllocator globalDescriptorAllocator;

    VkPipelineLayout gradientPipelineLayout;
    
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    std::vector<VkSemaphore> swapchainRenderSemaphores;
    std::vector<bool> swapchainImageInitialized;

	VkDescriptorSet drawImageDescriptors = VK_NULL_HANDLE;
	VkDescriptorSetLayout drawImageDescriptorLayout;
    uint32_t globalTextureCount{ 0 };
    std::map<std::pair<VkImageView, VkSampler>, int> globalTextureSlots;
    int addGlobalTexture(VkImageView view, VkSampler sampler);


    VmaAllocator allocator; // vma lib allocator

	VkDescriptorSetLayout gpuSceneDataDescriptorLayout;

    MeshPipelines meshPipelines;

    // draw resources
    AllocatedImage drawImage;
    AllocatedImage depthImage;

	AllocatedImage postProcessingImage;
	VkPipeline postProcessPipeline {VK_NULL_HANDLE};
	VkPipelineLayout postProcessPipelineLayout {VK_NULL_HANDLE};
	VkDescriptorSetLayout postProcessImageLayout {VK_NULL_HANDLE};

	VkSemaphore timelineSemaphore {VK_NULL_HANDLE};
	uint64_t timelineValue {0};

    DeletionQueue deletionQueue;

    UploadContext uploadContext;

	AllocatedImage whiteImage;
	AllocatedImage blackImage;
	AllocatedImage greyImage;
	AllocatedImage errorCheckerboardImage;

	VkSampler defaultSamplerLinear;
	VkSampler defaultSamplerNearest;
	
    DrawContext drawCommands;
    GPUSceneData sceneData;

    EngineStats stats;
    GpuProfiler gpuProfiler;

	std::vector<ComputeEffect> backgroundEffects;
	int currentBackgroundEffect{ 0 };

    // initializes everything in the engine
    void init();

	// run rendering code
	void run(const glm::mat4& viewMatrix);

    // shuts down the engine
    void cleanup();

    // draw loop
    void draw();

    void drawGeometry(VkCommandBuffer cmd);

	void handleImGui();
	void resizeSwapchain();

    FrameData& getCurrentFrame();
    uint32_t getCurrentFrameSlot() const { return frameNumber % FRAME_OVERLAP; }

    PFN_vkCmdPushDescriptorSetKHR vkCmdPushDescriptorSetKHR;

    std::unordered_map<std::string, std::shared_ptr<LoadedScene>> loadedScenes;

    float renderScale = 1;

    bool resizeRequested{false};
    VkPresentModeKHR presentMode { VK_PRESENT_MODE_FIFO_KHR };
    bool freezeRendering{false};
private:
    void updateScene(const glm::mat4& viewMatrix);
    bool beginFrame(uint32_t& swapchainImageIndex);
    VkCommandBuffer beginCommandBuffer();
    void recordBackgroundPass(VkCommandBuffer cmd);
    void recordGeometryPass(VkCommandBuffer cmd);
    void recordTonemapPass(VkCommandBuffer cmd);
    void recordBlitPass(VkCommandBuffer cmd, uint32_t swapchainImageIndex);
    void recordUiPass(VkCommandBuffer cmd, uint32_t swapchainImageIndex);
    void submitAndPresent(VkCommandBuffer cmd, uint32_t swapchainImageIndex);

    void initVulkan();

    void initSwapchain();

    void createSwapchain(uint32_t width, uint32_t height);

    void destroySwapchain();

    void createRenderTargets(VkExtent2D extent);
    void writeDrawImageDescriptor();

    void initCommands();

    void initPipelines();
    void initBackgroundPipelines();
	void initPostProcessPipeline();

    void initDescriptors();

    void initSyncStructures();

    void initRenderables();

    void initImGui();

    void initDefaultData();
};
