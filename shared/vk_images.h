
#pragma once 

#include <vulkan/vulkan.h>

namespace vkutil {


void transition_image(
	VkCommandBuffer cmd,
	VkImage image,
	VkImageLayout currentLayout,
	VkImageLayout newLayout,
	VkPipelineStageFlags2 srcStageMask,
	VkAccessFlags2 srcAccessMask,
	VkPipelineStageFlags2 dstStageMask,
	VkAccessFlags2 dstAccessMask,
	VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT);

void transition_image_for_compute_write(VkCommandBuffer cmd, VkImage image, VkImageLayout currentLayout, VkImageLayout newLayout);
void transition_image_for_color_attachment(VkCommandBuffer cmd, VkImage image, VkImageLayout currentLayout, VkImageLayout newLayout);
void transition_image_for_transfer_src(VkCommandBuffer cmd, VkImage image, VkImageLayout currentLayout, VkImageLayout newLayout);
void transition_image_for_transfer_dst(VkCommandBuffer cmd, VkImage image, VkImageLayout currentLayout, VkImageLayout newLayout);
void transition_image_for_shader_sampling(VkCommandBuffer cmd, VkImage image, VkImageLayout currentLayout, VkImageLayout newLayout);

void copy_image_to_image(VkCommandBuffer cmd, VkImage source, VkImage destination,VkExtent2D srcSize, VkExtent2D dstSize);

void generate_mipmaps(VkCommandBuffer cmd, VkImage image, VkExtent2D imageSize);
} // namespace vkutil
