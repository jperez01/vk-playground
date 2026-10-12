#include "vk_deletion_queue.h"

#include <cassert>

namespace {
    DeletionQueue* activeQueue = nullptr;
}

void DeletionQueue::pushBuffer(VkBuffer buffer, VmaAllocation allocation)
{
    entries.push_back({ pendingValue, buffer, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, allocation });
}

void DeletionQueue::pushImage(VkImage image, VkImageView view, VmaAllocation allocation)
{
    entries.push_back({ pendingValue, VK_NULL_HANDLE, image, view, VK_NULL_HANDLE, allocation });
}

void DeletionQueue::pushSampler(VkSampler sampler)
{
    entries.push_back({ pendingValue, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, sampler, VK_NULL_HANDLE });
}

void DeletionQueue::setPendingValue(uint64_t value)
{
    pendingValue = value;
}

void DeletionQueue::collect(VkDevice device, VmaAllocator allocator, uint64_t completedValue)
{
    while (!entries.empty() && entries.front().retireValue <= completedValue) {
        destroy(entries.front(), device, allocator);
        entries.pop_front();
    }
}

void DeletionQueue::flushAll(VkDevice device, VmaAllocator allocator)
{
    for (const Entry& entry : entries) {
        destroy(entry, device, allocator);
    }
    entries.clear();
}

void DeletionQueue::destroy(const Entry& entry, VkDevice device, VmaAllocator allocator)
{
    if (entry.view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, entry.view, nullptr);
    }
    if (entry.image != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, entry.image, entry.allocation);
    }
    if (entry.buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, entry.buffer, entry.allocation);
    }
    if (entry.sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, entry.sampler, nullptr);
    }
}

namespace vkutil {
    void setDeletionQueue(DeletionQueue* queue)
    {
        activeQueue = queue;
    }

    void deferDestroyBuffer(VkBuffer buffer, VmaAllocation allocation)
    {
        assert(activeQueue);
        activeQueue->pushBuffer(buffer, allocation);
    }

    void deferDestroyImage(VkImage image, VkImageView view, VmaAllocation allocation)
    {
        assert(activeQueue);
        activeQueue->pushImage(image, view, allocation);
    }

    void deferDestroySampler(VkSampler sampler)
    {
        assert(activeQueue);
        activeQueue->pushSampler(sampler);
    }
}
