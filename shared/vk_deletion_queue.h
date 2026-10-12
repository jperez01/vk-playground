// Deferred destruction of GPU objects, tagged with the timeline value that retires them.

#pragma once

#include <cstdint>
#include <deque>

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

// Destroys GPU objects once the GPU has finished every submit that could still use them.
class DeletionQueue {
public:
    void pushBuffer(VkBuffer buffer, VmaAllocation allocation);
    void pushImage(VkImage image, VkImageView view, VmaAllocation allocation);
    void pushSampler(VkSampler sampler);

    // Timeline value the next queue submit will signal; new entries wait for it.
    void setPendingValue(uint64_t value);

    // Destroys entries whose value is <= completedValue.
    void collect(VkDevice device, VmaAllocator allocator, uint64_t completedValue);
    void flushAll(VkDevice device, VmaAllocator allocator);

private:
    struct Entry {
        uint64_t retireValue;
        VkBuffer buffer;
        VkImage image;
        VkImageView view;
        VkSampler sampler;
        VmaAllocation allocation;
    };

    void destroy(const Entry& entry, VkDevice device, VmaAllocator allocator);

    std::deque<Entry> entries;
    uint64_t pendingValue { 1 };
};

namespace vkutil {
    // RAII resources hand their handles to this queue. Set once by the engine after device creation.
    void setDeletionQueue(DeletionQueue* queue);
    void deferDestroyBuffer(VkBuffer buffer, VmaAllocation allocation);
    void deferDestroyImage(VkImage image, VkImageView view, VmaAllocation allocation);
    void deferDestroySampler(VkSampler sampler);
}
