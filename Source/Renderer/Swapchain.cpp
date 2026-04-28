/*
 * Swapchain.cpp
 * Responsible for: swapchain creation, image retrieval, and image view management.
 * Vulkan concepts encapsulated: swapchain lifecycle, surface format selection,
 * present mode selection, extent calculation, image views.
 * Callers receive: image views, extent, format, and image count via accessors.
 * No VkFramebuffer here. Dynamic rendering does not need it.
 */

#include "Swapchain.hpp"
#include <iostream>
#include <algorithm>

Swapchain::Swapchain(VkPhysicalDevice physicalDevice, VkDevice device,
    VkSurfaceKHR surface, GLFWwindow* window)
    : physicalDevice_(physicalDevice), device_(device), surface_(surface), window_(window)
{
    create();
}

Swapchain::~Swapchain()
{
    destroy();
}

// Destroys and recreates the swapchain and image views at a new size.
void Swapchain::Recreate(uint32_t width, uint32_t height)
{
    destroy();
    create();
}

// Creates the swapchain, retrieves images, and creates image views.
void Swapchain::create()
{
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &caps);

    // Choose surface format. SRGB is preferred for correct gamma handling.
    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, formats.data());

    VkSurfaceFormatKHR chosenFormat = formats[0];
    for (const auto& f : formats)
    {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            chosenFormat = f;
            break;
        }
    }

    // Choose present mode. Mailbox gives triple buffering without tearing.
    // FIFO is the only guaranteed mode so it is the fallback.
    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &presentModeCount, presentModes.data());

    VkPresentModeKHR chosenPresentMode = VK_PRESENT_MODE_FIFO_KHR;
    for (const auto& pm : presentModes)
    {
        if (pm == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            chosenPresentMode = pm;
            break;
        }
    }

    // On HiDPI displays the framebuffer size in pixels differs from the window
    // size in screen coordinates, so we always query the framebuffer size.
    if (caps.currentExtent.width != UINT32_MAX)
    {
        extent_ = caps.currentExtent;
    }
    else
    {
        int w, h;
        glfwGetFramebufferSize(window_, &w, &h);
        extent_.width = std::clamp(static_cast<uint32_t>(w), caps.minImageExtent.width, caps.maxImageExtent.width);
        extent_.height = std::clamp(static_cast<uint32_t>(h), caps.minImageExtent.height, caps.maxImageExtent.height);
    }

    // Request one more image than the minimum so the CPU is never blocked
    // waiting for the driver to release an image.
    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
    {
        imageCount = caps.maxImageCount;
    }

    VkSwapchainCreateInfoKHR swapchainInfo{};
    swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainInfo.surface = surface_;
    swapchainInfo.minImageCount = imageCount;
    swapchainInfo.imageFormat = chosenFormat.format;
    swapchainInfo.imageColorSpace = chosenFormat.colorSpace;
    swapchainInfo.imageExtent = extent_;
    swapchainInfo.imageArrayLayers = 1;
    swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainInfo.preTransform = caps.currentTransform;
    swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainInfo.presentMode = chosenPresentMode;
    swapchainInfo.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(device_, &swapchainInfo, nullptr, &swapchain_) != VK_SUCCESS)
    {
        std::cerr << "Failed to create swapchain\n";
        return;
    }

    format_ = chosenFormat.format;

    uint32_t count = 0;
    vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr);
    images_.resize(count);
    vkGetSwapchainImagesKHR(device_, swapchain_, &count, images_.data());

    imageViews_.resize(images_.size());
    for (size_t i = 0; i < images_.size(); i++)
    {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = images_[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format_;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        // subresourceRange defines which part of the image this view covers.
        // For swapchain images that is always the full color image, one mip, one layer.
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device_, &viewInfo, nullptr, &imageViews_[i]) != VK_SUCCESS)
        {
            std::cerr << "Failed to create image view " << i << "\n";
        }
    }

    std::cout << "Swapchain created: " << extent_.width << "x"
        << extent_.height << ", " << images_.size() << " images\n";
}

// Destroys image views and the swapchain.
void Swapchain::destroy()
{
    for (VkImageView view : imageViews_)
    {
        vkDestroyImageView(device_, view, nullptr);
    }
    imageViews_.clear();
    images_.clear();

    if (swapchain_ != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
}