#include "vk_engine.h"

#include "vk_images.h"
#include "vk_loader.h"
#include "vk_descriptors.h"
#include "upload_batch.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include <vk_initializers.h>
#include <vk_types.h>

#include "VkBootstrap.h"

#include "imgui_impl_sdl2.h"
#include "imgui_impl_vulkan.h"

#include <glm/gtx/transform.hpp>

#define VMA_IMPLEMENTATION
#include <iostream>

#include "vk_mem_alloc.h"
#include "app/ui.h"
#include "utils/util.h"

#ifdef NDEBUG
constexpr bool bUseValidationLayers = false;
#else
constexpr bool bUseValidationLayers = true;
#endif

VulkanEngine* loadedEngine = nullptr;

VulkanEngine& VulkanEngine::Get()
{
    return *loadedEngine;
}

void VulkanEngine::init()
{
    // only one engine initialization is allowed with the application.
    assert(loadedEngine == nullptr);
    loadedEngine = this;

    // We initialize SDL and create a window with it.
    SDL_Init(SDL_INIT_VIDEO);

    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

    window = SDL_CreateWindow("Vulkan Engine", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, windowExtent.width,
        windowExtent.height, window_flags);

    initVulkan();

    initSwapchain();

    initCommands();

    initSyncStructures();

    initDescriptors();

    initDefaultData();

	initPipelines();

    initRenderables();

    initImGui();
}

void VulkanEngine::initDefaultData() {
	UploadBatch batch(uploadContext);

	//3 default textures, white, grey, black. 1 pixel each
	uint32_t white = glm::packUnorm4x8(glm::vec4(1, 1, 1, 1));
	whiteImage = batch.uploadImage((void*)&white, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
		VK_IMAGE_USAGE_SAMPLED_BIT, false);

	uint32_t grey = glm::packUnorm4x8(glm::vec4(0.66f, 0.66f, 0.66f, 1));
	greyImage = batch.uploadImage((void*)&grey, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
		VK_IMAGE_USAGE_SAMPLED_BIT, false);

	uint32_t black = glm::packUnorm4x8(glm::vec4(0, 0, 0, 0));
	blackImage = batch.uploadImage((void*)&black, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
		VK_IMAGE_USAGE_SAMPLED_BIT, false);

	//checkerboard image
	uint32_t magenta = glm::packUnorm4x8(glm::vec4(1, 0, 1, 1));
	std::array<uint32_t, 16 * 16 > pixels; //for 16x16 checkerboard texture
	for (int x = 0; x < 16; x++) {
		for (int y = 0; y < 16; y++) {
			pixels[y * 16 + x] = ((x % 2) ^ (y % 2)) ? magenta : black;
		}
	}

	errorCheckerboardImage = batch.uploadImage(pixels.data(), VkExtent3D{ 16, 16, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
		VK_IMAGE_USAGE_SAMPLED_BIT, false);
	batch.submitAndWait();

	VkSamplerCreateInfo sampl = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };

	sampl.magFilter = VK_FILTER_NEAREST;
	sampl.minFilter = VK_FILTER_NEAREST;

	vkCreateSampler(device, &sampl, nullptr, &defaultSamplerNearest);

	sampl.magFilter = VK_FILTER_LINEAR;
	sampl.minFilter = VK_FILTER_LINEAR;
	vkCreateSampler(device, &sampl, nullptr, &defaultSamplerLinear);
}

void VulkanEngine::cleanup()
{
    // make sure the gpu has stopped doing its things
    vkDeviceWaitIdle(device);

	metalRoughMaterial.clearResources(device);
	
	// Explicitly clear global resources so their RAII destructors fire before the device is destroyed
	drawImage = {};
	depthImage = {};
	postProcessingImage = {};
	whiteImage = {};
	blackImage = {};
	greyImage = {};
	errorCheckerboardImage = {};
    loadedScenes.clear();

    for (auto& frame : frames) {
        frame.sceneDataBuffer = {};
        frame.garbage.flush(device, allocator);
    }

	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();

	for (int i = 0; i < FRAME_OVERLAP; i++) {
		vkDestroyCommandPool(device, frames[i].commandPool, nullptr);
		vkDestroySemaphore(device, frames[i].swapchainSemaphore, nullptr);
	}
	vkDestroySemaphore(device, timelineSemaphore, nullptr);

	vkDestroyCommandPool(device, uploadContext.commandPool, nullptr);
	gpuProfiler.destroy(device);
	vkDestroyFence(device, uploadContext.fence, nullptr);

	if (defaultSamplerLinear != VK_NULL_HANDLE) {
		vkDestroySampler(device, defaultSamplerLinear, nullptr);
	}
	if (defaultSamplerNearest != VK_NULL_HANDLE) {
		vkDestroySampler(device, defaultSamplerNearest, nullptr);
	}

	for (auto& effect : backgroundEffects) {
		vkDestroyPipeline(device, effect.pipeline, nullptr);
	}
	vkDestroyPipelineLayout(device, gradientPipelineLayout, nullptr);

	vkDestroyPipeline(device, postProcessPipeline, nullptr);
	vkDestroyPipelineLayout(device, postProcessPipelineLayout, nullptr);

	vkDestroyDescriptorPool(device, globalDescriptorAllocator.pool, nullptr);
	vkDestroyDescriptorPool(device, imguiPool, nullptr);

	vkDestroyDescriptorSetLayout(device, drawImageDescriptorLayout, nullptr);
	vkDestroyDescriptorSetLayout(device, gpuSceneDataDescriptorLayout, nullptr);
	vkDestroyDescriptorSetLayout(device, postProcessImageLayout, nullptr);

    destroySwapchain();

    vkDestroySurfaceKHR(instance, surface, nullptr);

    vmaDestroyAllocator(allocator);

    vkDestroyDevice(device, nullptr);
    vkb::destroy_debug_utils_messenger(instance, debug_messenger);
    vkDestroyInstance(instance, nullptr);

    SDL_DestroyWindow(window);
}

void VulkanEngine::initBackgroundPipelines()
{
	VkPipelineLayoutCreateInfo computeLayout{};
	computeLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	computeLayout.pNext = nullptr;
	computeLayout.pSetLayouts = &drawImageDescriptorLayout;
	computeLayout.setLayoutCount = 1;

	VkPushConstantRange pushConstant{};
	pushConstant.offset = 0;
	pushConstant.size = sizeof(ComputePushConstants);
	pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	computeLayout.pPushConstantRanges = &pushConstant;
	computeLayout.pushConstantRangeCount = 1;

	VK_CHECK(vkCreatePipelineLayout(device, &computeLayout, nullptr, &gradientPipelineLayout));

	VkShaderModule gradientShader;
	if (!vkutil::loadShaderModule("gradient_color.comp", device, &gradientShader)) {
		fmt::print("Error when building the compute shader \n");
		abort();
	}

	VkShaderModule skyShader;
	if (!vkutil::loadShaderModule("sky.comp", device, &skyShader)) {
        fmt::print("Error when building the compute shader\n");
		abort();
	}

	VkPipelineShaderStageCreateInfo stageinfo{};
	stageinfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stageinfo.pNext = nullptr;
	stageinfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	stageinfo.module = gradientShader;
	stageinfo.pName = "main";

	VkComputePipelineCreateInfo computePipelineCreateInfo{};
	computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	computePipelineCreateInfo.pNext = nullptr;
	computePipelineCreateInfo.layout = gradientPipelineLayout;
	computePipelineCreateInfo.stage = stageinfo;

	ComputeEffect gradient{};
	gradient.layout = gradientPipelineLayout;
	gradient.name = "gradient";
	gradient.data = {};

	//default colors
	gradient.data.data1 = glm::vec4(1, 0, 0, 1);
	gradient.data.data2 = glm::vec4(0, 0, 1, 1);

	VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &gradient.pipeline));

	//change the shader module only to create the sky shader
	computePipelineCreateInfo.stage.module = skyShader;

	ComputeEffect sky{};
	sky.layout = gradientPipelineLayout;
	sky.name = "sky";
	sky.data = {};
	//default sky parameters
	sky.data.data1 = glm::vec4(0.1, 0.2, 0.4, 0.97);

	VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &sky.pipeline));

	//add the 2 background effects into the array
	backgroundEffects.push_back(gradient);
	backgroundEffects.push_back(sky);

	//destroy structures properly
	vkDestroyShaderModule(device, gradientShader, nullptr);
	vkDestroyShaderModule(device, skyShader, nullptr);
}


namespace vkutil {
    void deferDestroyBuffer(VkBuffer buffer, VmaAllocation allocation) {
        VulkanEngine::Get().deferDestroy(buffer, allocation);
    }
    void deferDestroyImage(VkImage image, VkImageView view, VmaAllocation allocation) {
        VulkanEngine::Get().deferDestroy(image, view, allocation);
    }
}

void FrameGarbage::flush(VkDevice device, VmaAllocator allocator) {
    // 1. Destroy Views (Child of Image)
    for (auto view : imageViews) {
        vkDestroyImageView(device, view, nullptr);
    }
    // 2. Destroy Images (Bound to Memory)
    for (auto image : images) {
        vkDestroyImage(device, image, nullptr);
    }
    // 3. Destroy Buffers (Bound to Memory)
    for (auto buffer : buffers) {
        vkDestroyBuffer(device, buffer, nullptr);
    }
    // 4. Free the actual Device Memory
    for (auto alloc : allocations) {
        vmaFreeMemory(allocator, alloc);
    }
    // 5. Samplers (Independent)
    for (auto sampler : samplers) {
        vkDestroySampler(device, sampler, nullptr);
    }
    
    buffers.clear();
    allocations.clear();
    images.clear();
    imageViews.clear();
    samplers.clear();
}

void VulkanEngine::deferDestroy(VkBuffer buffer, VmaAllocation allocation) {
    getCurrentFrame().garbage.buffers.push_back(buffer);
    getCurrentFrame().garbage.allocations.push_back(allocation);
}

void VulkanEngine::deferDestroy(VkImage image, VkImageView view, VmaAllocation allocation) {
    getCurrentFrame().garbage.images.push_back(image);
    getCurrentFrame().garbage.imageViews.push_back(view);
    getCurrentFrame().garbage.allocations.push_back(allocation);
}

void VulkanEngine::deferDestroy(VkSampler sampler) {
    getCurrentFrame().garbage.samplers.push_back(sampler);
}

bool VulkanEngine::beginFrame(uint32_t& swapchainImageIndex)
{
	// Wait for the submit that last used this frame slot (FRAME_OVERLAP frames ago),
	// so the CPU can record this frame while the GPU works on the previous one.
	uint64_t waitValue = getCurrentFrame().frameTimelineValue;
	if (waitValue > 0) {
		VkSemaphoreWaitInfo waitInfo{};
		waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
		waitInfo.pNext = nullptr;
		waitInfo.flags = 0;
		waitInfo.semaphoreCount = 1;
		waitInfo.pSemaphores = &timelineSemaphore;
		waitInfo.pValues = &waitValue;

		VK_CHECK(vkWaitSemaphores(device, &waitInfo, 1000000000));
	}

	const uint32_t slot = getCurrentFrameSlot();
	gpuProfiler.collect(device, slot);

	getCurrentFrame().garbage.flush(device, allocator);

	VkResult e = vkAcquireNextImageKHR(device, swapchain, 1000000000, getCurrentFrame().swapchainSemaphore, nullptr, &swapchainImageIndex);
	if (e == VK_ERROR_OUT_OF_DATE_KHR) {
		resizeRequested = true;
		return false;
	}

	drawExtent.height = std::min(swapchainExtent.height, drawImage.imageExtent.height) * renderScale;
	drawExtent.width = std::min(swapchainExtent.width, drawImage.imageExtent.width) * renderScale;
	return true;
}

VkCommandBuffer VulkanEngine::beginCommandBuffer()
{
	// the slot's previous submit has finished (waited in beginFrame), so the command buffer can be reset and re-recorded
	VkCommandBuffer cmd = getCurrentFrame().mainCommandBuffer;
	VK_CHECK(vkResetCommandBuffer(cmd, 0));

	// this command buffer is submitted exactly once per recording
	VkCommandBufferBeginInfo cmdBeginInfo = vkinit::commandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));
	gpuProfiler.reset(cmd, getCurrentFrameSlot());
	return cmd;
}

// Runs the background compute effect into the draw image.
void VulkanEngine::recordBackgroundPass(VkCommandBuffer cmd)
{
	ComputeEffect& effect = backgroundEffects[currentBackgroundEffect];
	const uint32_t slot = getCurrentFrameSlot();

	gpuProfiler.begin(cmd, slot, GpuPass::Background);

	// transition our main draw image into general layout so we can write into it
	// we will overwrite it all so we dont care about what was the older layout
	vkutil::transitionImageForComputeWrite(cmd, drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

	// bind the background compute pipeline
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, effect.pipeline);

	// bind the descriptor set containing the draw image for the compute pipeline
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, gradientPipelineLayout, 0, 1, &drawImageDescriptors, 0, nullptr);

	vkCmdPushConstants(cmd, gradientPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &effect.data);
	// execute the compute pipeline dispatch. We are using 16x16 workgroup size so we need to divide by it
	vkCmdDispatch(cmd, (uint32_t)std::ceil(drawExtent.width / 16.0), (uint32_t)std::ceil(drawExtent.height / 16.0), 1);
	gpuProfiler.end(cmd, slot, GpuPass::Background);
}

// Renders the scene geometry into the draw image with depth.
void VulkanEngine::recordGeometryPass(VkCommandBuffer cmd)
{
	const uint32_t slot = getCurrentFrameSlot();

	gpuProfiler.begin(cmd, slot, GpuPass::Geometry);
	vkutil::transitionImage(
		cmd,
		depthImage.image,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
		0,
		VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
		VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		VK_IMAGE_ASPECT_DEPTH_BIT);
	vkutil::transitionImageForColorAttachment(cmd, drawImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	VkClearValue clearValue{};
	clearValue.color = { {0.0f, 0.0f, 0.0f, 1.0f} };
	VkRenderingAttachmentInfo colorAttachment = vkinit::attachmentInfo(drawImage.imageView, &clearValue, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	VkRenderingAttachmentInfo depthAttachment = vkinit::depthAttachmentInfo(depthImage.imageView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

	VkRenderingInfo renderInfo = vkinit::renderingInfo(drawExtent, &colorAttachment, &depthAttachment);

	vkCmdBeginRendering(cmd, &renderInfo);
	auto start = std::chrono::steady_clock::now();
	drawGeometry(cmd);

	auto end = std::chrono::steady_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

	stats.geometryRecordTime = elapsed.count() / 1000.f;

	vkCmdEndRendering(cmd);
	gpuProfiler.end(cmd, slot, GpuPass::Geometry);
}

// Tonemaps the draw image into the post processing image with a compute dispatch.
void VulkanEngine::recordTonemapPass(VkCommandBuffer cmd)
{
	const uint32_t slot = getCurrentFrameSlot();

	//transition the draw image and post processing image to GENERAL for compute
	gpuProfiler.begin(cmd, slot, GpuPass::Tonemap);
	vkutil::transitionImage(cmd, drawImage.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
		VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);

	vkutil::transitionImage(cmd, postProcessingImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
		VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
		VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, postProcessPipeline);

	VkDescriptorImageInfo drawInfo{ .sampler = VK_NULL_HANDLE, .imageView = drawImage.imageView, .imageLayout = VK_IMAGE_LAYOUT_GENERAL };
	VkDescriptorImageInfo postInfo{ .sampler = VK_NULL_HANDLE, .imageView = postProcessingImage.imageView, .imageLayout = VK_IMAGE_LAYOUT_GENERAL };

	VkWriteDescriptorSet writes[2] = {};
	writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[0].dstBinding = 0;
	writes[0].descriptorCount = 1;
	writes[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	writes[0].pImageInfo = &drawInfo;

	writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[1].dstBinding = 1;
	writes[1].descriptorCount = 1;
	writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	writes[1].pImageInfo = &postInfo;

	vkCmdPushDescriptorSetKHR(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, postProcessPipelineLayout, 0, 2, writes);

	vkCmdDispatch(cmd, (uint32_t)std::ceil(drawExtent.width / 16.0), (uint32_t)std::ceil(drawExtent.height / 16.0), 1);

	gpuProfiler.end(cmd, slot, GpuPass::Tonemap);
}

// Copies the post processing image into the swapchain image.
void VulkanEngine::recordBlitPass(VkCommandBuffer cmd, uint32_t swapchainImageIndex)
{
	const uint32_t slot = getCurrentFrameSlot();

	//transition the post processing image and the swapchain image into their correct transfer layouts
	gpuProfiler.begin(cmd, slot, GpuPass::Blit);
	vkutil::transitionImage(cmd, postProcessingImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
		VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);

	VkImageLayout swapchainOldLayout = swapchainImageInitialized[swapchainImageIndex]
		? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
		: VK_IMAGE_LAYOUT_UNDEFINED;
	vkutil::transitionImage(
		cmd,
		swapchainImages[swapchainImageIndex],
		swapchainOldLayout,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
		0,
		VK_PIPELINE_STAGE_2_TRANSFER_BIT,
		VK_ACCESS_2_TRANSFER_WRITE_BIT);

	// execute a copy from the post processing image into the swapchain
	vkutil::copyImageToImage(cmd, postProcessingImage.image, swapchainImages[swapchainImageIndex], drawExtent,swapchainExtent);

	gpuProfiler.end(cmd, slot, GpuPass::Blit);
}

// Draws the ImGui overlay on top of the swapchain image.
void VulkanEngine::recordUiPass(VkCommandBuffer cmd, uint32_t swapchainImageIndex)
{
	const uint32_t slot = getCurrentFrameSlot();

	// set swapchain image layout to Attachment Optimal so we can draw it
	gpuProfiler.begin(cmd, slot, GpuPass::Ui);
	vkutil::transitionImage(
		cmd,
		swapchainImages[swapchainImageIndex],
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_2_TRANSFER_BIT,
		VK_ACCESS_2_TRANSFER_WRITE_BIT,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

	VkRenderingAttachmentInfo colorAttachment = vkinit::attachmentInfo(swapchainImageViews[swapchainImageIndex], nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	VkRenderingInfo renderInfo = vkinit::renderingInfo(swapchainExtent, &colorAttachment, nullptr);

	vkCmdBeginRendering(cmd, &renderInfo);

	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

	vkCmdEndRendering(cmd);
	gpuProfiler.end(cmd, slot, GpuPass::Ui);
}

// Finishes the command buffer, submits it and presents the swapchain image.
void VulkanEngine::submitAndPresent(VkCommandBuffer cmd, uint32_t swapchainImageIndex)
{
	// set swapchain image layout to Present so we can show it
	vkutil::transitionImage(
		cmd,
		swapchainImages[swapchainImageIndex],
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
		VK_PIPELINE_STAGE_2_NONE,
		VK_ACCESS_2_NONE);
	swapchainImageInitialized[swapchainImageIndex] = true;

	//finalize the command buffer (we can no longer add commands, but it can now be executed)
	VK_CHECK(vkEndCommandBuffer(cmd));

	//the submit waits on the swapchain acquire semaphore and signals the per-image render semaphore plus the frame timeline

	VkCommandBufferSubmitInfo cmdinfo = vkinit::commandBufferSubmitInfo(cmd);

	timelineValue++;
	getCurrentFrame().frameTimelineValue = timelineValue;

	VkSemaphoreSubmitInfo waitInfo = vkinit::semaphoreSubmitInfo(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, getCurrentFrame().swapchainSemaphore);

	VkSemaphoreSubmitInfo signalInfos[2];
	signalInfos[0] = vkinit::semaphoreSubmitInfo(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, swapchainRenderSemaphores[swapchainImageIndex]);
	signalInfos[1] = vkinit::semaphoreSubmitInfo(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, timelineSemaphore);
	signalInfos[1].value = timelineValue;

	VkSubmitInfo2 submit = vkinit::submitInfo(&cmdinfo, 2, signalInfos, 1, &waitInfo);

	VK_CHECK(vkQueueSubmit2(graphicsQueue, 1, &submit, VK_NULL_HANDLE));

	// present the image we just rendered to, waiting on its render semaphore
	// so drawing commands have finished before the image is displayed to the user
	VkPresentInfoKHR presentInfo = vkinit::presentInfo();

	presentInfo.pSwapchains = &swapchain;
	presentInfo.swapchainCount = 1;

	presentInfo.pWaitSemaphores = &swapchainRenderSemaphores[swapchainImageIndex];
	presentInfo.waitSemaphoreCount = 1;

	presentInfo.pImageIndices = &swapchainImageIndex;

	VkResult presentResult = vkQueuePresentKHR(graphicsQueue, &presentInfo);
	if (presentResult == VK_ERROR_OUT_OF_DATE_KHR) {
		resizeRequested = true;
		return;
	}
	//increase the number of frames drawn
	frameNumber++;
}

void VulkanEngine::draw()
{
	uint32_t swapchainImageIndex;
	if (!beginFrame(swapchainImageIndex)) {
		return;
	}
	VkCommandBuffer cmd = beginCommandBuffer();

	recordBackgroundPass(cmd);
	recordGeometryPass(cmd);
	recordTonemapPass(cmd);
	recordBlitPass(cmd, swapchainImageIndex);
	recordUiPass(cmd, swapchainImageIndex);

	submitAndPresent(cmd, swapchainImageIndex);
}

bool isVisible(const RenderObject& obj, const glm::mat4& viewproj) {
    std::array<glm::vec3, 8> corners {
        glm::vec3 { 1, 1, 1 },
        glm::vec3 { 1, 1, -1 },
        glm::vec3 { 1, -1, 1 },
        glm::vec3 { 1, -1, -1 },
        glm::vec3 { -1, 1, 1 },
        glm::vec3 { -1, 1, -1 },
        glm::vec3 { -1, -1, 1 },
        glm::vec3 { -1, -1, -1 },
    };

    glm::mat4 matrix = viewproj * obj.transform;

    glm::vec3 min = { 1.5, 1.5, 1.5 };
    glm::vec3 max = { -1.5, -1.5, -1.5 };

    for (int c = 0; c < 8; c++) {
        // project each corner into clip space
        glm::vec4 v = matrix * glm::vec4(obj.bounds.origin + (corners[c] * obj.bounds.extents), 1.f);

        // perspective correction
        v.x = v.x / v.w;
        v.y = v.y / v.w;
        v.z = v.z / v.w;

        min = glm::min(glm::vec3 { v.x, v.y, v.z }, min);
        max = glm::max(glm::vec3 { v.x, v.y, v.z }, max);
    }

    // check the clip space box is within the view
    if (min.z > 1.f || max.z < 0.f || min.x > 1.f || max.x < -1.f || min.y > 1.f || max.y < -1.f) {
        return false;
    } else {
        return true;
    }
}

void VulkanEngine::drawGeometry(VkCommandBuffer cmd)
{
    std::vector<uint32_t> opaque_draws;
    opaque_draws.reserve(drawCommands.OpaqueSurfaces.size());

    for (int i = 0; i < drawCommands.OpaqueSurfaces.size(); i++) {
       if (isVisible(drawCommands.OpaqueSurfaces[i], sceneData.viewproj)) {
            opaque_draws.push_back(i);
       }
    }

    // sort the opaque surfaces by material and mesh
    std::sort(opaque_draws.begin(), opaque_draws.end(), [&](const auto& iA, const auto& iB) {
		const RenderObject& A = drawCommands.OpaqueSurfaces[iA];
		const RenderObject& B = drawCommands.OpaqueSurfaces[iB];
        if (A.material == B.material) {
            return A.indexBuffer < B.indexBuffer;
        } else {
            return A.material < B.material;
        }
    });

    //write the buffer
    GPUSceneData* sceneUniformData = (GPUSceneData*)getCurrentFrame().sceneDataBuffer.allocation->GetMappedData();
    *sceneUniformData = sceneData;


    MaterialPipeline* lastPipeline = nullptr;
    MaterialInstance* lastMaterial = nullptr;
    VkBuffer lastIndexBuffer = VK_NULL_HANDLE;

    auto bindPipeline = [&](MaterialPipeline* pipeline) {
        lastPipeline = pipeline;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->layout, 0, 1,
            &getCurrentFrame().globalDescriptor, 0, nullptr);

        VkViewport viewport = {};
        viewport.x = 0;
        viewport.y = 0;
        viewport.width = (float)drawExtent.width;
        viewport.height = (float)drawExtent.height;
        viewport.minDepth = 0.f;
        viewport.maxDepth = 1.f;

        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor = {};
        scissor.offset.x = 0;
        scissor.offset.y = 0;
        scissor.extent.width = drawExtent.width;
        scissor.extent.height = drawExtent.height;

        vkCmdSetScissor(cmd, 0, 1, &scissor);
    };

    auto draw = [&](const RenderObject& r) {
        if (r.material != lastMaterial) {
            lastMaterial = r.material;
            if (r.material->pipeline != lastPipeline) {
                bindPipeline(r.material->pipeline);
            }
        }
        if (r.indexBuffer != lastIndexBuffer) {
            lastIndexBuffer = r.indexBuffer;
            vkCmdBindIndexBuffer(cmd, r.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        }
        // calculate final mesh matrix
        GPUDrawPushConstants push_constants{};
        push_constants.worldMatrix = r.transform;
        push_constants.vertexBuffer = r.vertexBufferAddress;
        push_constants.materialBuffer = r.material->materialBuffer;

        vkCmdPushConstants(cmd, r.material->pipeline->layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(GPUDrawPushConstants), &push_constants);

        stats.drawcallCount++;
        stats.triangleCount += r.indexCount / 3;
        vkCmdDrawIndexed(cmd, r.indexCount, 1, r.firstIndex, 0, 0);
    };

    stats.drawcallCount = 0;
    stats.triangleCount = 0;

    // Dynamic state: opaque pass — back-face cull, depth test+write on
    vkCmdSetCullMode(cmd, VK_CULL_MODE_BACK_BIT);
    vkCmdSetFrontFace(cmd, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    vkCmdSetDepthTestEnable(cmd, VK_TRUE);
    vkCmdSetDepthWriteEnable(cmd, VK_TRUE);
    vkCmdSetDepthCompareOp(cmd, VK_COMPARE_OP_GREATER_OR_EQUAL);

    for (auto& r : opaque_draws) {
        draw(drawCommands.OpaqueSurfaces[r]);
    }

    // Dynamic state: transparent pass — no cull, depth test on but writes off
    vkCmdSetCullMode(cmd, VK_CULL_MODE_NONE);
    vkCmdSetFrontFace(cmd, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    vkCmdSetDepthTestEnable(cmd, VK_TRUE);
    vkCmdSetDepthWriteEnable(cmd, VK_FALSE);
    vkCmdSetDepthCompareOp(cmd, VK_COMPARE_OP_GREATER_OR_EQUAL);
    for (auto& r : drawCommands.TransparentSurfaces) {
        draw(r);
    }

    // we delete the draw commands now that we processed them
    drawCommands.OpaqueSurfaces.clear();
    drawCommands.TransparentSurfaces.clear();
}

void VulkanEngine::run()
{
	auto start = std::chrono::steady_clock::now();
	if (resizeRequested) {
		resizeSwapchain();
	}
	handleImGui();

	draw();

	auto end = std::chrono::steady_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

	stats.frametime = elapsed.count() / 1000.f;
}

void VulkanEngine::updateScene(const glm::mat4& viewMatrix)
{
	// camera projection
	glm::mat4 projection = glm::perspective(glm::radians(70.f), (float)drawExtent.width / (float)drawExtent.height, 10000.f, 0.1f);

	// invert the Y direction on projection matrix so that we are more similar
	// to opengl and gltf axis
	projection[1][1] *= -1;

	sceneData.view = viewMatrix;
	sceneData.proj = projection;
	sceneData.viewproj = projection * viewMatrix;

    for (auto& [name, scene] : loadedScenes) {
        scene->Draw(glm::mat4{ 1.f }, drawCommands);
    }
}

FrameData& VulkanEngine::getCurrentFrame()
{
    return frames[frameNumber % FRAME_OVERLAP];
}

void VulkanEngine::initVulkan()
{
    vkb::InstanceBuilder builder;

    // make the vulkan instance, with basic debug features
    auto inst_ret = builder.set_app_name("Example Vulkan Application")
                        .request_validation_layers(bUseValidationLayers)
                        .use_default_debug_messenger()
                        .require_api_version(1, 3, 0);
    
    // Enable enhanced validation features when validation layers are active
    // Best practices validation warns about non-optimal Vulkan usage patterns
    // Synchronization validation catches synchronization errors and over-broad barriers
    if (bUseValidationLayers) {
        inst_ret.add_validation_feature_enable(VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT)
                .add_validation_feature_enable(VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT);
        // VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT is optional but expensive
    }
    
    auto instance_result = inst_ret.build();

    vkb::Instance vkb_inst = instance_result.value();

    // grab the instance
    instance = vkb_inst.instance;
    debug_messenger = vkb_inst.debug_messenger;

    SDL_Vulkan_CreateSurface(window, instance, &surface);

    VkPhysicalDeviceVulkan13Features features13 {};
	features13.dynamicRendering = true;
	features13.synchronization2 = true;
	features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

   VkPhysicalDeviceVulkan12Features features12 {};
   features12.bufferDeviceAddress = true;
   features12.descriptorIndexing = true;
   features12.descriptorBindingPartiallyBound = true;
   features12.descriptorBindingSampledImageUpdateAfterBind = true;
   features12.runtimeDescriptorArray = true;
   features12.timelineSemaphore = true;
   features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;

   VkPhysicalDeviceVulkan11Features features11 {};
   features11.shaderDrawParameters = true;
   features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;

    // use vkbootstrap to select a gpu.
    // We want a gpu that can write to the SDL surface and supports vulkan 1.2
    vkb::PhysicalDeviceSelector selector { vkb_inst };
    vkb::PhysicalDevice physicalDevice = selector.set_minimum_version(1, 3)
        .set_required_features_13(features13)
        .set_required_features_12(features12)
        .set_required_features_11(features11)
        .set_surface(surface)
        .add_required_extension(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME)
        .select()
        .value();


    // physicalDevice.features.
    // create the final vulkan device

    vkb::DeviceBuilder deviceBuilder { physicalDevice };
    vkb::Device vkbDevice = deviceBuilder.build().value();
    // Get the VkDevice handle used in the rest of a vulkan application
    device = vkbDevice.device;
    chosenGPU = physicalDevice.physical_device;
    
    vkCmdPushDescriptorSetKHR = (PFN_vkCmdPushDescriptorSetKHR)vkGetDeviceProcAddr(device, "vkCmdPushDescriptorSetKHR");

    // use vkbootstrap to get a Graphics queue
    graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();

    graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

    gpuProfiler.init(instance, device, chosenGPU, graphicsQueueFamily, FRAME_OVERLAP);

    // initialize the memory allocator
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = chosenGPU;
    allocatorInfo.device = device;
    allocatorInfo.instance = instance;
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    vmaCreateAllocator(&allocatorInfo, &allocator);

    uploadContext.device = device;
    uploadContext.allocator = allocator;
    uploadContext.queue = graphicsQueue;
}

void VulkanEngine::initSwapchain()
{
    createSwapchain(windowExtent.width, windowExtent.height);

    createRenderTargets(windowExtent);
}

void VulkanEngine::createRenderTargets(VkExtent2D extent)
{
	VkExtent3D size = { extent.width, extent.height, 1 };

	drawImage = vkutil::createImage(device, allocator, size, VK_FORMAT_R16G16B16A16_SFLOAT,
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, false);

	depthImage = vkutil::createImage(device, allocator, size, VK_FORMAT_D32_SFLOAT,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, false);

	postProcessingImage = vkutil::createImage(device, allocator, size, VK_FORMAT_R16G16B16A16_SFLOAT,
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, false);
}

void VulkanEngine::writeDrawImageDescriptor()
{
	if (drawImageDescriptors == VK_NULL_HANDLE) {
		return;
	}

	VkDescriptorImageInfo imgInfo{};
	imgInfo.imageView = drawImage.imageView;
	imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

	VkWriteDescriptorSet write{};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = drawImageDescriptors;
	write.dstBinding = 0;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	write.pImageInfo = &imgInfo;

	vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}


void VulkanEngine::createSwapchain(uint32_t width, uint32_t height)
{
	vkb::SwapchainBuilder swapchainBuilder{ chosenGPU,device,surface };

	swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

	vkb::Swapchain vkbSwapchain = swapchainBuilder
		//.use_default_format_selection()
		.set_desired_format(VkSurfaceFormatKHR{ .format = swapchainImageFormat, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
		//falls back to FIFO (vsync) if the desired mode is unsupported
		.set_desired_present_mode(presentMode)
		.set_desired_extent(width, height)
		.add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
		.build()
		.value();

	swapchainExtent = vkbSwapchain.extent;
	//store swapchain and its related images
	swapchain = vkbSwapchain.swapchain;
	swapchainImages = vkbSwapchain.get_images().value();
	swapchainImageViews = vkbSwapchain.get_image_views().value();
	swapchainImageInitialized.assign(swapchainImages.size(), false);

	VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphoreCreateInfo();
	swapchainRenderSemaphores.resize(swapchainImages.size());
	for (size_t i = 0; i < swapchainRenderSemaphores.size(); i++) {
		VK_CHECK(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &swapchainRenderSemaphores[i]));
	}
}
void VulkanEngine::destroySwapchain()
{
	for (size_t i = 0; i < swapchainRenderSemaphores.size(); i++) {
		if (swapchainRenderSemaphores[i] != VK_NULL_HANDLE) {
			vkDestroySemaphore(device, swapchainRenderSemaphores[i], nullptr);
		}
	}
	swapchainRenderSemaphores.clear();
	swapchainImageInitialized.clear();

	vkDestroySwapchainKHR(device, swapchain, nullptr);

	// destroy swapchain resources
	for (int i = 0; i < swapchainImageViews.size(); i++) {
		vkDestroyImageView(device, swapchainImageViews[i], nullptr);
	}
}

void VulkanEngine::resizeSwapchain()
{
    if (timelineValue > 0) {
        VkSemaphoreWaitInfo waitInfo{};
        waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        waitInfo.pNext = nullptr;
        waitInfo.flags = 0;
        waitInfo.semaphoreCount = 1;
        waitInfo.pSemaphores = &timelineSemaphore;
        waitInfo.pValues = &timelineValue;

        VK_CHECK(vkWaitSemaphores(device, &waitInfo, 1000000000));
    }

	int w, h;
	SDL_GetWindowSize(window, &w, &h);
	if (w == 0 || h == 0) {
		return;
	}

	destroySwapchain();

	windowExtent.width = w;
	windowExtent.height = h;

	createSwapchain(windowExtent.width, windowExtent.height);
	createRenderTargets(windowExtent);
	writeDrawImageDescriptor();
	resizeRequested = false;
}

void VulkanEngine::initCommands()
{
    // create a command pool for commands submitted to the graphics queue.
    // we also want the pool to allow for resetting of individual command buffers
    VkCommandPoolCreateInfo commandPoolInfo = vkinit::commandPoolCreateInfo(graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

    for (int i = 0; i < FRAME_OVERLAP; i++) {

        VK_CHECK(vkCreateCommandPool(device, &commandPoolInfo, nullptr, &frames[i].commandPool));

        // allocate the default command buffer that we will use for rendering
        VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::commandBufferAllocateInfo(frames[i].commandPool, 1);

        VK_CHECK(vkAllocateCommandBuffers(device, &cmdAllocInfo, &frames[i].mainCommandBuffer));

    }

    VK_CHECK(vkCreateCommandPool(device, &commandPoolInfo, nullptr, &uploadContext.commandPool));

    // allocate the default command buffer that we will use for rendering
    VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::commandBufferAllocateInfo(uploadContext.commandPool, 1);

    VK_CHECK(vkAllocateCommandBuffers(device, &cmdAllocInfo, &uploadContext.commandBuffer));

}

void VulkanEngine::initSyncStructures()
{
    // create syncronization structures
    // one fence to control when the gpu has finished rendering the frame,
    // and 2 semaphores to syncronize rendering with swapchain
    // we want the fence to start signalled so we can wait on it on the first
    // frame
    VkFenceCreateInfo fenceCreateInfo = vkinit::fenceCreateInfo(VK_FENCE_CREATE_SIGNALED_BIT);
    VK_CHECK(vkCreateFence(device, &fenceCreateInfo, nullptr, &uploadContext.fence));

    VkSemaphoreTypeCreateInfo timelineCreateInfo{};
    timelineCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    timelineCreateInfo.pNext = nullptr;
    timelineCreateInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    timelineCreateInfo.initialValue = 0;

    VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphoreCreateInfo();
    semaphoreCreateInfo.pNext = &timelineCreateInfo;

    VK_CHECK(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &timelineSemaphore));

    semaphoreCreateInfo.pNext = nullptr;

    for (int i = 0; i < FRAME_OVERLAP; i++) {
        VK_CHECK(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &frames[i].swapchainSemaphore));


    }
}

void VulkanEngine::initRenderables()
{
	auto structureStart = std::chrono::steady_clock::now();
    std::string structurePath = std::string(ASSET_DIR) + "structure.glb";
    auto structureFile = loadGltf(this,structurePath);

    assert(structureFile.has_value());

    loadedScenes["structure"] = *structureFile;
	auto structureEnd = std::chrono::steady_clock::now();
	auto structureElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(structureEnd - structureStart).count();
	fmt::println("Time elapsed loading structure: {} ms", structureElapsed);

}

void VulkanEngine::initImGui()
{
    // 1: create descriptor pool for IMGUI
    //  the size of the pool is very oversize, but it's copied from imgui demo
    //  itself.
    VkDescriptorPoolSize pool_sizes[] = { { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 } };

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000;
    pool_info.poolSizeCount = (uint32_t)std::size(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;

    VK_CHECK(vkCreateDescriptorPool(device, &pool_info, nullptr, &imguiPool));

    // 2: initialize imgui library

	// this initializes the core structures of imgui
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.ConfigFlags ^= ImGuiConfigFlags_IsSRGB;

	// this initializes imgui for SDL
	ImGui_ImplSDL2_InitForVulkan(window);

	// this initializes imgui for Vulkan
	ImGui_ImplVulkan_InitInfo init_info = {};
	init_info.Instance = instance;
	init_info.PhysicalDevice = chosenGPU;
	init_info.Device = device;
	init_info.Queue = graphicsQueue;
	init_info.DescriptorPool = imguiPool;
	init_info.MinImageCount = 3;
	init_info.ImageCount = 3;
	init_info.UseDynamicRendering = true;

	//dynamic rendering parameters for imgui to use
	init_info.PipelineRenderingCreateInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
	init_info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
	init_info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &swapchainImageFormat;

	init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

	ImGui_ImplVulkan_Init(&init_info);

}

void VulkanEngine::initPipelines()
{
    // COMPUTE PIPELINES
    initBackgroundPipelines();
    initPostProcessPipeline();

    metalRoughMaterial.buildPipelines(this);
}

void VulkanEngine::initDescriptors()
{
    // create a descriptor pool
    std::vector<DescriptorAllocator::PoolSizeRatio> sizes = {
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 30 },
    };

    globalDescriptorAllocator.initPool(device, 100, sizes);

    {
        DescriptorLayoutBuilder builder;
        builder.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        drawImageDescriptorLayout = builder.build(device, VK_SHADER_STAGE_COMPUTE_BIT);
    }
    {
        DescriptorLayoutBuilder builder;
        builder.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        builder.addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_GLOBAL_TEXTURES); // Global texture array

        // textures can be added while the other frame is still in flight
        VkDescriptorBindingFlags bindFlags[2] = { 0, VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT };
        VkDescriptorSetLayoutBindingFlagsCreateInfo extendedInfo = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO };
        extendedInfo.bindingCount = 2;
        extendedInfo.pBindingFlags = bindFlags;
        
        gpuSceneDataDescriptorLayout = builder.build(device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, &extendedInfo, VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT);
    }


    drawImageDescriptors = globalDescriptorAllocator.allocate(device, drawImageDescriptorLayout);
    writeDrawImageDescriptor();

    for (int i = 0; i < FRAME_OVERLAP; i++) {
        frames[i].globalDescriptor = globalDescriptorAllocator.allocate(device, gpuSceneDataDescriptorLayout);
        frames[i].sceneDataBuffer = vkutil::createBuffer(allocator, sizeof(GPUSceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, vkutil::HOST_WRITE_MAPPED);

        VkDescriptorBufferInfo bufInfo{};
        bufInfo.buffer = frames[i].sceneDataBuffer.buffer;
        bufInfo.offset = 0;
        bufInfo.range = sizeof(GPUSceneData);

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = frames[i].globalDescriptor;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &bufInfo;

        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }
}

void GLTFMetallic_Roughness::buildPipelines(VulkanEngine* engine)
{
	VkShaderModule meshFragShader;
	if (!vkutil::loadShaderModule("basic.frag", engine->device, &meshFragShader)) {
		fmt::println("Error when building the triangle fragment shader module");
		abort();
	}

	VkShaderModule meshVertexShader;
	if (!vkutil::loadShaderModule("basic.vert", engine->device, &meshVertexShader)) {
		fmt::println("Error when building the triangle vertex shader module");
		abort();
	}

	VkPushConstantRange matrixRange{};
	matrixRange.offset = 0;
	matrixRange.size = sizeof(GPUDrawPushConstants);
	matrixRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;


	VkDescriptorSetLayout layouts[] = { engine->gpuSceneDataDescriptorLayout };

	VkPipelineLayoutCreateInfo mesh_layout_info = vkinit::pipelineLayoutCreateInfo();
	mesh_layout_info.setLayoutCount = 1;
	mesh_layout_info.pSetLayouts = layouts;
	mesh_layout_info.pPushConstantRanges = &matrixRange;
	mesh_layout_info.pushConstantRangeCount = 1;

	VkPipelineLayout newLayout;
	VK_CHECK(vkCreatePipelineLayout(engine->device, &mesh_layout_info, nullptr, &newLayout));

    opaquePipeline.layout = newLayout;
    transparentPipeline.layout = newLayout;

	// build the stage-create-info for both vertex and fragment stages. This lets
	// the pipeline know the shader modules per stage
	PipelineBuilder pipelineBuilder;

	pipelineBuilder.setShaders(meshVertexShader, meshFragShader);

	pipelineBuilder.setInputTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

	pipelineBuilder.setPolygonMode(VK_POLYGON_MODE_FILL);

	// NOTE: cull mode, front face, depth test/write/compare are now dynamic state
	// and will be set per-pass in drawGeometry() via vkCmdSet* calls.

	pipelineBuilder.setMultisamplingNone();

	pipelineBuilder.disableBlending();

	//render format
	pipelineBuilder.setColorAttachmentFormat(engine->drawImage.imageFormat);
	pipelineBuilder.setDepthFormat(engine->depthImage.imageFormat);

	// use the triangle layout we created
	pipelineBuilder.pipelineLayout = newLayout;

	// finally build the pipeline
    opaquePipeline.pipeline = pipelineBuilder.buildPipeline(engine->device);

	// create the transparent variant (only blending differs; depth/cull are dynamic)
	pipelineBuilder.enableBlendingAdditive();

	transparentPipeline.pipeline = pipelineBuilder.buildPipeline(engine->device);

	vkDestroyShaderModule(engine->device, meshFragShader, nullptr);
	vkDestroyShaderModule(engine->device, meshVertexShader, nullptr);
}

void VulkanEngine::initPostProcessPipeline() {
	VkDescriptorSetLayoutBinding drawBind{};
	drawBind.binding = 0;
	drawBind.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	drawBind.descriptorCount = 1;
	drawBind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	VkDescriptorSetLayoutBinding postBind{};
	postBind.binding = 1;
	postBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	postBind.descriptorCount = 1;
	postBind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	VkDescriptorSetLayoutBinding bindings[] = { drawBind, postBind };

	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR; 
	layoutInfo.bindingCount = 2;
	layoutInfo.pBindings = bindings;

	VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &postProcessImageLayout));

	VkPipelineLayoutCreateInfo pipelineLayoutInfo = vkinit::pipelineLayoutCreateInfo();
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pSetLayouts = &postProcessImageLayout;

	VK_CHECK(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &postProcessPipelineLayout));

	VkShaderModule computeShader;
	if (!vkutil::loadShaderModule("post_process.comp", device, &computeShader)) {
		fmt::print("Error when loading the post_process compute shader module\n");
		abort();
	}

	VkPipelineShaderStageCreateInfo stageinfo{};
	stageinfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stageinfo.pNext = nullptr;
	stageinfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	stageinfo.module = computeShader;
	stageinfo.pName = "main";

	VkComputePipelineCreateInfo computePipelineCreateInfo{};
	computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	computePipelineCreateInfo.pNext = nullptr;
	computePipelineCreateInfo.layout = postProcessPipelineLayout;
	computePipelineCreateInfo.stage = stageinfo;

	VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &postProcessPipeline));

	vkDestroyShaderModule(device, computeShader, nullptr);
}

void VulkanEngine::handleImGui() {
	// imgui new frame
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplSDL2_NewFrame();

	ImGui::NewFrame();

	Editor::handleUi(this);

	ImGui::Begin("Stats");

	ImGui::Text("frametime %f ms", stats.frametime);
	ImGui::Text("geometry record (CPU) %f ms", stats.geometryRecordTime);

	if (gpuProfiler.supported()) {
		ImGui::SeparatorText("GPU (ms)");
		ImGui::Text("total %.3f", gpuProfiler.totalMs());
		for (uint32_t i = 0; i < (uint32_t)GpuPass::Count; i++) {
			GpuPass p = (GpuPass)i;
			ImGui::Text("%s %.3f", GpuProfiler::passName(p), gpuProfiler.passMs(p));
		}
	} else {
		ImGui::Text("GPU timestamps not supported");
	}

	const char* presentModeNames[] = { "FIFO (vsync)", "MAILBOX", "IMMEDIATE" };
	const VkPresentModeKHR presentModes[] = { VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR };
	int currentPresentMode = 0;
	for (int i = 0; i < 3; i++) {
		if (presentModes[i] == presentMode) currentPresentMode = i;
	}
	if (ImGui::Combo("Present mode", &currentPresentMode, presentModeNames, 3)) {
		presentMode = presentModes[currentPresentMode];
		resizeRequested = true;
	}

	ImGui::Text("triangles %i", stats.triangleCount);
	ImGui::Text("draws %i", stats.drawcallCount);
	ImGui::End();

	ImGui::Begin("background");

	ComputeEffect& selected = backgroundEffects[currentBackgroundEffect];

	ImGui::Text("Selected effect: %s",selected.name);

	ImGui::SliderInt("Effect Index", &currentBackgroundEffect, 0, backgroundEffects.size() - 1);

	ImGui::InputFloat4("data1", (float*)&selected.data.data1);
	ImGui::InputFloat4("data2", (float*)&selected.data.data2);
	ImGui::InputFloat4("data3", (float*)&selected.data.data3);
	ImGui::InputFloat4("data4", (float*)&selected.data.data4);

	ImGui::SliderFloat4("Sunlight Color", (float*)&sceneData.sunlightColor, 0.0f, 1.0f);
	ImGui::SliderFloat4("Sunlight Direction", (float*)&sceneData.sunlightDirection, -1.0f, 1.0f);

	ImGui::End();

	ImGui::Render();
}

void GLTFMetallic_Roughness::clearResources(VkDevice device)
{
	if (opaquePipeline.pipeline != VK_NULL_HANDLE) {
		vkDestroyPipeline(device, opaquePipeline.pipeline, nullptr);
		opaquePipeline.pipeline = VK_NULL_HANDLE;
	}
	if (transparentPipeline.pipeline != VK_NULL_HANDLE) {
		vkDestroyPipeline(device, transparentPipeline.pipeline, nullptr);
		transparentPipeline.pipeline = VK_NULL_HANDLE;
	}

	VkPipelineLayout layoutToDestroy = VK_NULL_HANDLE;
	if (opaquePipeline.layout != VK_NULL_HANDLE) {
		layoutToDestroy = opaquePipeline.layout;
	} else if (transparentPipeline.layout != VK_NULL_HANDLE) {
		layoutToDestroy = transparentPipeline.layout;
	}
	if (layoutToDestroy != VK_NULL_HANDLE) {
		vkDestroyPipelineLayout(device, layoutToDestroy, nullptr);
	}
	opaquePipeline.layout = VK_NULL_HANDLE;
	transparentPipeline.layout = VK_NULL_HANDLE;
}

void MeshNode::Draw(const glm::mat4& topMatrix, DrawContext& ctx)
{
    glm::mat4 nodeMatrix = topMatrix * worldTransform;

    for (auto& s : mesh->surfaces) {
        RenderObject def;
        def.indexCount = s.count;
        def.firstIndex = s.startIndex;
        def.indexBuffer = mesh->meshBuffers->indexBuffer.buffer;
        def.material = &s.material->data;
        def.bounds = s.bounds;
        def.transform = nodeMatrix;
        def.vertexBufferAddress = mesh->meshBuffers->vertexBufferAddress;

        if (s.material->data.passType == MaterialPass::Transparent) {
            ctx.TransparentSurfaces.push_back(def);
        } else {
            ctx.OpaqueSurfaces.push_back(def);
        }
    }

    // recurse down
    Node::Draw(topMatrix, ctx);
}
int VulkanEngine::addGlobalTexture(VkImageView view, VkSampler sampler)
{
    auto existing = globalTextureSlots.find({ view, sampler });
    if (existing != globalTextureSlots.end()) {
        return existing->second;
    }
    if (globalTextureCount >= MAX_GLOBAL_TEXTURES) {
        fmt::println("Global texture array is full ({} slots)", MAX_GLOBAL_TEXTURES);
        abort();
    }
    int index = globalTextureCount++;
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = view;
    imageInfo.sampler = sampler;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstBinding = 1;
    write.dstArrayElement = index;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;

    VkWriteDescriptorSet writes[FRAME_OVERLAP];
    for (int i = 0; i < FRAME_OVERLAP; i++) {
        writes[i] = write;
        writes[i].dstSet = frames[i].globalDescriptor;
    }
    vkUpdateDescriptorSets(device, FRAME_OVERLAP, writes, 0, nullptr);
    globalTextureSlots[{ view, sampler }] = index;
    return index;
}










