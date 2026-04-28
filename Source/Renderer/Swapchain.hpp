/*
 * Swapchain.hpp
 * Responsible for: swapchain creation, image retrieval, and image view management.
 * Vulkan concepts encapsulated: swapchain lifecycle, surface format selection,
 * present mode selection, extent calculation, image views.
 * Callers receive: image views, extent, format, and image count via accessors.
 * No VkFramebuffer here. Dynamic rendering does not need it.
 */

#pragma once

#include <vector>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

class Swapchain
{
public:
    Swapchain(VkPhysicalDevice physicalDevice, VkDevice device,
        VkSurfaceKHR surface, GLFWwindow* window);
    ~Swapchain();

    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;
    Swapchain(Swapchain&&) = delete;
    Swapchain& operator=(Swapchain&&) = delete;

    void Recreate(uint32_t width, uint32_t height);

    VkSwapchainKHR              Handle()                     const { return swapchain_; }
    VkFormat                    Format()                     const { return format_; }
    VkExtent2D                  Extent()                     const { return extent_; }
    uint32_t                    ImageCount()                 const { return static_cast<uint32_t>(images_.size()); }
    VkImage                     Image(uint32_t index)        const { return images_[index]; }
    VkImageView                 ImageView(uint32_t index)    const { return imageViews_[index]; }
    const std::vector<VkImage>& Images()                     const { return images_; }

private:
    void create();
    void destroy();

    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice         device_ = VK_NULL_HANDLE;
    VkSurfaceKHR     surface_ = VK_NULL_HANDLE;
    GLFWwindow* window_ = nullptr;

    VkSwapchainKHR           swapchain_ = VK_NULL_HANDLE;
    VkFormat                 format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D               extent_ = {};
    std::vector<VkImage>     images_;
    std::vector<VkImageView> imageViews_;
};