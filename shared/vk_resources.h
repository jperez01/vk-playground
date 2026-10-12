// GPU buffer and image creation helpers.
#pragma once

#include <vk_mem_alloc.h>

#include "vk_types.h"

namespace vkutil {
    constexpr VmaAllocationCreateFlags HOST_WRITE_MAPPED = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    AllocatedBuffer createBuffer(VmaAllocator allocator, size_t allocSize, VkBufferUsageFlags usage, VmaAllocationCreateFlags allocFlags = 0);
    AllocatedImage createImage(VkDevice device, VmaAllocator allocator, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool hasMipMaps);
}
