#include "gpu_profiler.h"

void GpuProfiler::init(VkInstance instance, VkDevice device, VkPhysicalDevice gpu, uint32_t queueFamily, uint32_t framesInFlight)
{
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(gpu, &props);
    timestampPeriod = props.limits.timestampPeriod;

    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &familyCount, families.data());
    validBits = queueFamily < familyCount ? families[queueFamily].timestampValidBits : 0;

    beginLabel = (PFN_vkCmdBeginDebugUtilsLabelEXT)vkGetInstanceProcAddr(instance, "vkCmdBeginDebugUtilsLabelEXT");
    endLabel = (PFN_vkCmdEndDebugUtilsLabelEXT)vkGetInstanceProcAddr(instance, "vkCmdEndDebugUtilsLabelEXT");

    timestampsSupported = validBits > 0 && timestampPeriod > 0.f;
    if (!timestampsSupported) return;

    VkQueryPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    info.queryCount = framesInFlight * 2 * PassCount;
    VK_CHECK(vkCreateQueryPool(device, &info, nullptr, &queryPool));

    pendingResults.assign(framesInFlight, false);
}

void GpuProfiler::destroy(VkDevice device)
{
    if (queryPool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(device, queryPool, nullptr);
        queryPool = VK_NULL_HANDLE;
    }
}

uint32_t GpuProfiler::query_index(uint32_t frameSlot, GpuPass pass, bool isEnd) const
{
    return frameSlot * 2 * PassCount + (uint32_t)pass * 2 + (isEnd ? 1 : 0);
}

void GpuProfiler::collect(VkDevice device, uint32_t frameSlot)
{
    if (!timestampsSupported || !pendingResults[frameSlot]) return;
    pendingResults[frameSlot] = false;

    uint64_t data[2 * PassCount];
    VkResult result = vkGetQueryPoolResults(device, queryPool, frameSlot * 2 * PassCount, 2 * PassCount,
        sizeof(data), data, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
    if (result != VK_SUCCESS) return;

    if (validBits < 64) {
        const uint64_t mask = (1ull << validBits) - 1;
        for (uint64_t& v : data) v &= mask;
    }

    const float toMs = timestampPeriod / 1e6f;
    for (uint32_t i = 0; i < PassCount; i++) {
        float sample = (float)(data[i * 2 + 1] - data[i * 2]) * toMs;
        smoothedMs[i] = hasSample ? smoothedMs[i] * 0.9f + sample * 0.1f : sample;
    }
    float total = (float)(data[2 * PassCount - 1] - data[0]) * toMs;
    smoothedTotal = hasSample ? smoothedTotal * 0.9f + total * 0.1f : total;
    hasSample = true;
}

void GpuProfiler::reset(VkCommandBuffer cmd, uint32_t frameSlot)
{
    if (!timestampsSupported) return;
    vkCmdResetQueryPool(cmd, queryPool, frameSlot * 2 * PassCount, 2 * PassCount);
    pendingResults[frameSlot] = true;
}

void GpuProfiler::begin(VkCommandBuffer cmd, uint32_t frameSlot, GpuPass pass)
{
    if (beginLabel) {
        static const float colors[PassCount][4] = {
            { 0.3f, 0.6f, 0.9f, 1.f },
            { 0.9f, 0.5f, 0.2f, 1.f },
            { 0.4f, 0.8f, 0.4f, 1.f },
            { 0.8f, 0.4f, 0.8f, 1.f },
            { 0.9f, 0.8f, 0.3f, 1.f },
        };
        VkDebugUtilsLabelEXT label{};
        label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        label.pLabelName = pass_name(pass);
        for (int i = 0; i < 4; i++) label.color[i] = colors[(uint32_t)pass][i];
        beginLabel(cmd, &label);
    }
    if (timestampsSupported) {
        vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, queryPool, query_index(frameSlot, pass, false));
    }
}

void GpuProfiler::end(VkCommandBuffer cmd, uint32_t frameSlot, GpuPass pass)
{
    if (timestampsSupported) {
        vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, queryPool, query_index(frameSlot, pass, true));
    }
    if (endLabel) {
        endLabel(cmd);
    }
}

float GpuProfiler::pass_ms(GpuPass pass) const
{
    return smoothedMs[(uint32_t)pass];
}

float GpuProfiler::total_ms() const
{
    return smoothedTotal;
}

const char* GpuProfiler::pass_name(GpuPass pass)
{
    switch (pass) {
    case GpuPass::Background: return "Background";
    case GpuPass::Geometry: return "Geometry";
    case GpuPass::Tonemap: return "Tonemap";
    case GpuPass::Blit: return "Blit to swapchain";
    case GpuPass::Ui: return "ImGui";
    default: return "Unknown";
    }
}
