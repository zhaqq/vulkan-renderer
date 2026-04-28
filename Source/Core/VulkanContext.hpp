/*
 * VulkanContext.hpp
 * Responsible for: Vulkan instance, validation layers, debug messenger, surface,
 * physical device selection, logical device, and queue handle ownership.
 * Vulkan concepts encapsulated: instance/device lifecycle, queue families,
 * Vulkan 1.2 and 1.3 feature enablement.
 * Callers receive: device, physical device, queue, queue family index, and surface
 * via const accessors. All Vulkan handles are owned and destroyed by this class.
 */

#pragma once

#include <vector>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

class VulkanContext
{
public:
    explicit VulkanContext(GLFWwindow* window);
    ~VulkanContext();

    // Non-copyable, non-movable. Vulkan handles are not trivially transferable.
    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;
    VulkanContext(VulkanContext&&) = delete;
    VulkanContext& operator=(VulkanContext&&) = delete;

    VkInstance               Instance()               const { return instance_; }
    VkPhysicalDevice         PhysicalDevice()         const { return physicalDevice_; }
    VkDevice                 Device()                 const { return device_; }
    VkQueue                  GraphicsQueue()           const { return graphicsQueue_; }
    VkQueue                  PresentQueue()            const { return presentQueue_; }
    uint32_t                 GraphicsQueueFamilyIndex() const { return graphicsQueueFamily_; }
    VkSurfaceKHR             Surface()                const { return surface_; }

private:
    void createInstance();
    void createDebugMessenger();
    void destroyDebugMessenger();
    void createSurface(GLFWwindow* window);
    void pickPhysicalDevice();
    void createLogicalDevice();

    bool isDeviceSuitable(VkPhysicalDevice device) const;
    int  scoreDevice(VkPhysicalDevice device)       const;
    uint32_t findQueueFamily(VkPhysicalDevice device) const;

    VkInstance               instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR             surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice         physicalDevice_ = VK_NULL_HANDLE;
    VkDevice                 device_ = VK_NULL_HANDLE;
    VkQueue                  graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue                  presentQueue_ = VK_NULL_HANDLE;
    uint32_t                 graphicsQueueFamily_ = UINT32_MAX;

    const std::vector<const char*> validationLayers_ = { "VK_LAYER_KHRONOS_validation" };
    const std::vector<const char*> deviceExtensions_ = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
};