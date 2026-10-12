#include "upload_batch.h"

#include "vk_engine.h"
#include "vk_images.h"
#include "vk_initializers.h"
#include "utils/util.h"

UploadBatch::UploadBatch(VulkanEngine& engine) : engine(engine), cmd(engine.immCommandBuffer)
{
    assert(!engine.uploadBatchOpen);
    engine.uploadBatchOpen = true;

    VK_CHECK(vkResetFences(engine.device, 1, &engine.immFence));
    VK_CHECK(vkResetCommandBuffer(cmd, 0));

    VkCommandBufferBeginInfo cmdBeginInfo = vkinit::commandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));
}

UploadBatch::~UploadBatch()
{
    if (!submitted) {
        submitAndWait();
    }
}

GPUMeshBuffers UploadBatch::uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices)
{
    const size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
    const size_t indexBufferSize = indices.size() * sizeof(uint32_t);

    GPUMeshBuffers newSurface;

    newSurface.vertexBuffer = vkutil::createBuffer(engine.allocator, vertexBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

    VkBufferDeviceAddressInfo deviceAdressInfo{.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,.buffer = newSurface.vertexBuffer.buffer};
    newSurface.vertexBufferAddress = vkGetBufferDeviceAddress(engine.device, &deviceAdressInfo);

    newSurface.indexBuffer = vkutil::createBuffer(engine.allocator, indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

    AllocatedBuffer staging = vkutil::createBuffer(engine.allocator, vertexBufferSize + indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vkutil::HOST_WRITE_MAPPED);

    void* data = staging.info.pMappedData;

    // copy vertex buffer
    memcpy(data, vertices.data(), vertexBufferSize);
    // copy index buffer
    memcpy((char*)data + vertexBufferSize, indices.data(), indexBufferSize);

    VkBufferCopy vertexCopy { 0 };
    vertexCopy.dstOffset = 0;
    vertexCopy.srcOffset = 0;
    vertexCopy.size = vertexBufferSize;

    vkCmdCopyBuffer(cmd, staging.buffer, newSurface.vertexBuffer.buffer, 1, &vertexCopy);

    VkBufferCopy indexCopy { 0 };
    indexCopy.dstOffset = 0;
    indexCopy.srcOffset = vertexBufferSize;
    indexCopy.size = indexBufferSize;

    vkCmdCopyBuffer(cmd, staging.buffer, newSurface.indexBuffer.buffer, 1, &indexCopy);

    stagingBuffers.push_back(std::move(staging));

    return newSurface;
}

AllocatedImage UploadBatch::uploadImage(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped)
{
    size_t data_size = size.depth * size.width * size.height * 4;
    AllocatedBuffer uploadbuffer = vkutil::createBuffer(engine.allocator, data_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vkutil::HOST_WRITE_MAPPED);

    memcpy(uploadbuffer.info.pMappedData, data, data_size);

    AllocatedImage new_image = vkutil::createImage(engine.device, engine.allocator, size, format, usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, mipmapped);

    vkutil::transitionImageForTransferDst(cmd, new_image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkBufferImageCopy copyRegion = {};
    copyRegion.bufferOffset = 0;
    copyRegion.bufferRowLength = 0;
    copyRegion.bufferImageHeight = 0;

    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.imageSubresource.mipLevel = 0;
    copyRegion.imageSubresource.baseArrayLayer = 0;
    copyRegion.imageSubresource.layerCount = 1;
    copyRegion.imageExtent = size;

    // copy the buffer into the image
    vkCmdCopyBufferToImage(cmd, uploadbuffer.buffer, new_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
        &copyRegion);

    if (mipmapped) {
        vkutil::generateMipmaps(cmd, new_image.image, VkExtent2D{new_image.imageExtent.width, new_image.imageExtent.height});
    } else {
        vkutil::transitionImageForShaderSampling(cmd, new_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    stagingBuffers.push_back(std::move(uploadbuffer));
    return new_image;
}

void UploadBatch::submitAndWait()
{
    VK_CHECK(vkEndCommandBuffer(cmd));

    VkCommandBufferSubmitInfo cmdinfo = vkinit::commandBufferSubmitInfo(cmd);
    VkSubmitInfo2 submit = vkinit::submitInfo(&cmdinfo, nullptr, nullptr);

    VK_CHECK(vkQueueSubmit2(engine.graphicsQueue, 1, &submit, engine.immFence));
    VK_CHECK(vkWaitForFences(engine.device, 1, &engine.immFence, true, 9999999999));

    submitted = true;
    engine.uploadBatchOpen = false;
    stagingBuffers.clear();
}
