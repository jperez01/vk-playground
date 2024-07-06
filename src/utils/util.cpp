//
// Created by jpabl on 06/07/2024.
//

#include "util.h"

#include "vk_initializers.h"

AllocatedBuffer vkutil::createBuffer(VmaAllocator allocator, size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage) {
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.pNext = nullptr;
    bufferInfo.size = allocSize;
    bufferInfo.usage = usage;

    VmaAllocationCreateInfo createInfo = {};
    createInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
    createInfo.usage = memoryUsage;

    AllocatedBuffer newBuffer = {};

    VK_CHECK(
        vmaCreateBuffer(allocator, &bufferInfo, &createInfo, &newBuffer.buffer, &newBuffer.allocation, &newBuffer.info)
        );

    return newBuffer;
}

AllocatedImage vkutil::createImage(VkDevice device, VmaAllocator allocator, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool hasMipMaps) {
    AllocatedImage image = {};
    image.imageFormat = format;
    image.imageExtent = size;

    VkImageCreateInfo imgInfo = vkinit::image_create_info(format, usage, size);
    if (hasMipMaps) {
        imgInfo.mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(size.width, size.height)))) + 1;
    }

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    allocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VK_CHECK(
        vmaCreateImage(allocator, &imgInfo, &allocInfo, &image.image, &image.allocation, nullptr)
        );

    VkImageAspectFlags aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
    if (format == VK_FORMAT_D32_SFLOAT) {
        aspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT;
    }

    VkImageViewCreateInfo viewInfo = vkinit::imageview_create_info(format, image.image, aspectFlags);
    viewInfo.subresourceRange.levelCount = imgInfo.mipLevels;

    VK_CHECK(
        vkCreateImageView(device, &viewInfo, nullptr, &image.imageView)
        );

    return image;
}

void vkutil::destroyImage(VkDevice device, VmaAllocator allocator, const AllocatedImage& image) {
    vkDestroyImageView(device, image.imageView, nullptr);
    vmaDestroyImage(allocator, image.image, image.allocation);
}
void vkutil::destroyBuffer(VmaAllocator allocator, const AllocatedBuffer& buffer) {
    vmaDestroyBuffer(allocator, buffer.buffer, buffer.allocation);
}