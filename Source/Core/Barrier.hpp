/*
 * Barrier.hpp
 * Responsible for: sync2 image layout transition helpers.
 * Vulkan concepts encapsulated: VkImageMemoryBarrier2, VkDependencyInfo,
 * pipeline stage and access masks.
 * Callers pass a command buffer, image, and layout transition parameters.
 * No state is stored here. All functions are stateless helpers.
 */

#pragma once
#include <vulkan/vulkan.h>

namespace Barrier
{
    // Inserts a sync2 image layout transition barrier into the command buffer.
    // sync2 co-locates src/dst stage and access masks per barrier, which is
    // easier to reason about than the legacy split-struct form.
    inline void ImageLayoutTransition(
        VkCommandBuffer     commandBuffer,
        VkImage             image,
        VkImageLayout       oldLayout,
        VkImageLayout       newLayout,
        VkPipelineStageFlags2 srcStage,
        VkAccessFlags2        srcAccess,
        VkPipelineStageFlags2 dstStage,
        VkAccessFlags2        dstAccess)
    {
        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = srcStage;
        barrier.srcAccessMask = srcAccess;
        barrier.dstStageMask = dstStage;
        barrier.dstAccessMask = dstAccess;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.image = image;
        barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

        VkDependencyInfo dep{};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &barrier;

        vkCmdPipelineBarrier2(commandBuffer, &dep);
    }
}