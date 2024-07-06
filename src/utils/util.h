//
// Created by jpabl on 06/07/2024.
//
#pragma once

#include <vk_mem_alloc.h>

#include "vk_types.h"

namespace vkutil {
    AllocatedBuffer createBuffer(VmaAllocator allocator, size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
    AllocatedImage createImage(VkDevice device, VmaAllocator allocator, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool hasMipMaps);

    void destroyImage(VkDevice device, VmaAllocator allocator, const AllocatedImage& image);
    void destroyBuffer(VmaAllocator allocator, const AllocatedBuffer& buffer);
}