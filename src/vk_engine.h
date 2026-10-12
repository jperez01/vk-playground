// VulkanEngine: owns the Vulkan device, swapchain, per-frame resources and the frame loop.

#pragma once

#include <vk_types.h>

#include <deque>
#include <functional>
#include <map>
#include <utility>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <vk_mem_alloc.h>
 
#include <camera.h>
#include "gpu_profiler.h"
#include <vk_descriptors.h>
#include <vk_loader.h>
#include <vk_pipelines.h>

#include "model/model.h"

class Model;

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

struct RenderObject {
    uint32_t indexCount;
    uint32_t firstIndex;
    VkBuffer indexBuffer;
    
    MaterialInstance* material;
    Bounds bounds;
    glm::mat4 transform;
    VkDeviceAddress vertexBufferAddress;
};

struct FrameGarbage {
    std::vector<VkBuffer> buffers;
    std::vector<VmaAllocation> allocations;
    std::vector<VkImage> images;
    std::vector<VkImageView> imageViews;
    std::vector<VkSampler> samplers;

    void flush(VkDevice device, VmaAllocator allocator);
};

struct FrameData {
	VkSemaphore swapchainSemaphore;
	uint64_t frameTimelineValue {0};
    FrameGarbage garbage;
    AllocatedBuffer sceneDataBuffer;
    VkDescriptorSet globalDescriptor;

    VkCommandPool commandPool;
    VkCommandBuffer mainCommandBuffer;
};

constexpr unsigned int FRAME_OVERLAP = 2;
constexpr uint32_t MAX_GLOBAL_TEXTURES = 1000;


struct DrawContext {
    std::vector<RenderObject> OpaqueSurfaces;
    std::vector<RenderObject> TransparentSurfaces;
};

struct EngineStats {
    float frametime;
    int triangleCount;
    int drawcallCount;
    float geometryRecordTime;
};

struct GLTFMetallic_Roughness {
    MaterialPipeline opaquePipeline;
    MaterialPipeline transparentPipeline;


    void buildPipelines(VulkanEngine* engine);
    void clearResources(VkDevice device);

};

struct MeshNode : public Node {

	std::shared_ptr<MeshAsset> mesh;

	virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx) override;
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
    AllocatedBuffer defaultGLTFMaterialData;
    VkDeviceAddress defaultMaterialAddress {0};

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

	VkDescriptorSet drawImageDescriptors;
	VkDescriptorSetLayout drawImageDescriptorLayout;
    uint32_t globalTextureCount{ 0 };
    std::map<std::pair<VkImageView, VkSampler>, int> globalTextureSlots;
    int addGlobalTexture(VkImageView view, VkSampler sampler);


    VmaAllocator allocator; // vma lib allocator

	VkDescriptorSetLayout gpuSceneDataDescriptorLayout;

    GLTFMetallic_Roughness metalRoughMaterial;

    // draw resources
    AllocatedImage drawImage;
    AllocatedImage depthImage;

	AllocatedImage postProcessingImage;
	VkPipeline postProcessPipeline {VK_NULL_HANDLE};
	VkPipelineLayout postProcessPipelineLayout {VK_NULL_HANDLE};
	VkDescriptorSetLayout postProcessImageLayout {VK_NULL_HANDLE};

	VkSemaphore timelineSemaphore {VK_NULL_HANDLE};
	uint64_t timelineValue {0};

    // immediate submit structures
    VkFence immFence;
    VkCommandBuffer immCommandBuffer;
    VkCommandPool immCommandPool;

	AllocatedImage whiteImage;
	AllocatedImage blackImage;
	AllocatedImage greyImage;
	AllocatedImage errorCheckerboardImage;

	VkSampler defaultSamplerLinear;
	VkSampler defaultSamplerNearest;
	
    DrawContext drawCommands;
    GPUSceneData sceneData;

    Camera mainCamera;

    EngineStats stats;
    GpuProfiler gpuProfiler;

	std::vector<std::unique_ptr<Model>> importedModels;

	std::vector<ComputeEffect> backgroundEffects;
	int currentBackgroundEffect{ 0 };

    // singleton style getter.multiple engines is not supported
    static VulkanEngine& Get();

    // initializes everything in the engine
    void init();

	// run rendering code
	void run();

    // shuts down the engine
    void cleanup();

    // draw loop
    void draw();
	void drawMain(VkCommandBuffer cmd);
	void drawImGui(VkCommandBuffer cmd, VkImageView targetImageView);

	void renderNodes();

    void drawGeometry(VkCommandBuffer cmd);

    void updateScene(const glm::mat4& viewMatrix);
	void sendModelDataToGpu(Model& model);

    // upload a mesh into a pair of gpu buffers. If descriptor allocator is not
    // null, it will also create a descriptor that points to the vertex buffer
	GPUMeshBuffers uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices);
	GPUMeshBuffers uploadMesh(VkCommandBuffer cmd, std::vector<AllocatedBuffer>& stagingBuffers, std::span<uint32_t> indices, std::span<Vertex> vertices);
	void handleImGui();
	void resizeSwapchain();

    FrameData& getCurrentFrame();
    uint32_t getCurrentFrameSlot() const { return frameNumber % FRAME_OVERLAP; }

    AllocatedImage createImage(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
    AllocatedImage createImage(VkCommandBuffer cmd, std::vector<AllocatedBuffer>& stagingBuffers, void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);

    void deferDestroy(VkBuffer buffer, VmaAllocation allocation);
    void deferDestroy(VkImage image, VkImageView view, VmaAllocation allocation);
    void deferDestroy(VkSampler sampler);

    PFN_vkCmdPushDescriptorSetKHR vkCmdPushDescriptorSetKHR;

    void immediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function);

    std::unordered_map<std::string, std::shared_ptr<LoadedGLTF>> loadedScenes;

    float renderScale = 1;

    bool resizeRequested{false};
    VkPresentModeKHR presentMode { VK_PRESENT_MODE_FIFO_KHR };
    bool freezeRendering{false};
private:
    void initVulkan();

    void initSwapchain();

    void createSwapchain(uint32_t width, uint32_t height);

    void destroySwapchain();

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
