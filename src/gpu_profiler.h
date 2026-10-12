#pragma once
#include <vk_types.h>
#include <array>
#include <vector>

enum class GpuPass : uint32_t { Background, Geometry, Tonemap, Blit, Ui, Count };

// Measures GPU time per pass with timestamp queries, one query range per frame in flight,
// and wraps each pass in a debug-utils label for frame captures.
class GpuProfiler {
public:
    void init(VkInstance instance, VkDevice device, VkPhysicalDevice gpu, uint32_t queueFamily, uint32_t framesInFlight);
    void destroy(VkDevice device);

    // Call after the CPU has waited for this frame slot's previous submit.
    void collect(VkDevice device, uint32_t frameSlot);
    // Call at the start of recording, outside any rendering scope.
    void reset(VkCommandBuffer cmd, uint32_t frameSlot);
    void begin(VkCommandBuffer cmd, uint32_t frameSlot, GpuPass pass);
    void end(VkCommandBuffer cmd, uint32_t frameSlot, GpuPass pass);

    bool supported() const { return _supported; }
    float pass_ms(GpuPass pass) const;
    float total_ms() const;
    static const char* pass_name(GpuPass pass);

private:
    static constexpr uint32_t PassCount = (uint32_t)GpuPass::Count;

    uint32_t query_index(uint32_t frameSlot, GpuPass pass, bool isEnd) const;

    bool _supported{ false };
    VkQueryPool _pool{ VK_NULL_HANDLE };
    float _timestampPeriod{ 0.f };
    uint32_t _validBits{ 0 };

    std::vector<bool> _pending;
    std::array<float, PassCount> _smoothedMs{};
    float _smoothedTotal{ 0.f };
    bool _hasSample{ false };

    PFN_vkCmdBeginDebugUtilsLabelEXT _beginLabel{ nullptr };
    PFN_vkCmdEndDebugUtilsLabelEXT _endLabel{ nullptr };
};
