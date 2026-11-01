
#pragma once 

#include <vulkan/vulkan.h>

namespace vkutil {


void transitionImage(
	VkCommandBuffer cmd,
	VkImage image,
	VkImageLayout currentLayout,
	VkImageLayout newLayout,
	VkPipelineStageFlags2 srcStageMask,
	VkAccessFlags2 srcAccessMask,
	VkPipelineStageFlags2 dstStageMask,
	VkAccessFlags2 dstAccessMask,
	VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT);

void transitionImageForColorAttachment(VkCommandBuffer cmd, VkImage image, VkImageLayout currentLayout, VkImageLayout newLayout);
void transitionImageForTransferSrc(VkCommandBuffer cmd, VkImage image, VkImageLayout currentLayout, VkImageLayout newLayout);
void transitionImageForTransferDst(VkCommandBuffer cmd, VkImage image, VkImageLayout currentLayout, VkImageLayout newLayout);
void transitionImageForShaderSampling(VkCommandBuffer cmd, VkImage image, VkImageLayout currentLayout, VkImageLayout newLayout);

void copyImageToImage(VkCommandBuffer cmd, VkImage source, VkImage destination,VkExtent2D srcSize, VkExtent2D dstSize);

void generateMipmaps(VkCommandBuffer cmd, VkImage image, VkExtent2D imageSize);
} // namespace vkutil
