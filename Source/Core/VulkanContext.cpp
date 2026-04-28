/*
 * VulkanContext.cpp
 * Responsible for: Vulkan instance, validation layers, debug messenger, surface,
 * physical device selection, logical device, and queue handle ownership.
 * Vulkan concepts encapsulated: instance/device lifecycle, queue families,
 * Vulkan 1.2 and 1.3 feature enablement.
 * Callers receive: device, physical device, queue, queue family index, and surface
 * via const accessors. All Vulkan handles are owned and destroyed by this class.
 */

#include "VulkanContext.hpp"
#include <iostream>
#include <string>
#include <vector>

#ifdef NDEBUG
constexpr bool enableValidation = false;
#else
constexpr bool enableValidation = true;
#endif

// Routes Vulkan validation messages to stderr.
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData)
{
    std::cerr << "[Vulkan] " << pCallbackData->pMessage << "\n";
    return VK_FALSE;
}

VulkanContext::VulkanContext(GLFWwindow* window)
{
    createInstance();
    if (enableValidation)
    {
        createDebugMessenger();
    }
    createSurface(window);
    pickPhysicalDevice();
    createLogicalDevice();
}

VulkanContext::~VulkanContext()
{
    // Destroy in reverse creation order.
    vkDestroyDevice(device_, nullptr);
    vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (enableValidation)
    {
        destroyDebugMessenger();
    }
    vkDestroyInstance(instance_, nullptr);
}

// Creates the Vulkan instance with required extensions and optional validation layers.
void VulkanContext::createInstance()
{
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    if (enableValidation)
    {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Vulkan Renderer";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    // Request 1.3 features. The SDK is 1.4 but all features this project
    // needs are in 1.3, and 1.3 has wider driver support.
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &appInfo;
    instanceInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    instanceInfo.ppEnabledExtensionNames = extensions.data();

    if (enableValidation)
    {
        instanceInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers_.size());
        instanceInfo.ppEnabledLayerNames = validationLayers_.data();
    }

    if (vkCreateInstance(&instanceInfo, nullptr, &instance_) != VK_SUCCESS)
    {
        std::cerr << "Failed to create Vulkan instance\n";
    }
}

// Registers the debug messenger so validation messages route to debugCallback.
void VulkanContext::createDebugMessenger()
{
    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;

    // Extension function, must be looked up at runtime.
    if (auto func = (PFN_vkCreateDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"))
    {
        if (func(instance_, &createInfo, nullptr, &debugMessenger_) != VK_SUCCESS)
        {
            std::cerr << "Failed to create debug messenger\n";
        }
    }
}

// Unregisters and destroys the debug messenger. Must be called before vkDestroyInstance.
void VulkanContext::destroyDebugMessenger()
{
    if (auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"))
    {
        func(instance_, debugMessenger_, nullptr);
    }
}

// Creates the window surface. Must exist before physical device selection.
void VulkanContext::createSurface(GLFWwindow* window)
{
    if (glfwCreateWindowSurface(instance_, window, nullptr, &surface_) != VK_SUCCESS)
    {
        std::cerr << "Failed to create window surface\n";
    }
}

// Returns true if the device supports all required Vulkan 1.2, 1.3 features and extensions.
bool VulkanContext::isDeviceSuitable(VkPhysicalDevice device) const
{
    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;

    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.pNext = &features12;

    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &features13;

    vkGetPhysicalDeviceFeatures2(device, &features2);

    if (!features13.dynamicRendering || !features13.synchronization2 ||
        !features12.bufferDeviceAddress || !features12.descriptorIndexing ||
        !features12.timelineSemaphore)
    {
        return false;
    }

    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> available(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, available.data());

    for (const char* required : deviceExtensions_)
    {
        bool found = false;
        for (const auto& ext : available)
        {
            if (std::string(ext.extensionName) == required)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            return false;
        }
    }

    return true;
}

// Scores a physical device. Higher is better. Returns 0 if unsuitable.
int VulkanContext::scoreDevice(VkPhysicalDevice device) const
{
    if (!isDeviceSuitable(device))
    {
        return 0;
    }

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(device, &props);

    int score = 0;
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
    {
        score += 1000;
    }
    else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
    {
        score += 100;
    }

    return score;
}

// Returns the queue family index supporting both graphics and present, or UINT32_MAX.
uint32_t VulkanContext::findQueueFamily(VkPhysicalDevice device) const
{
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());

    for (uint32_t i = 0; i < count; i++)
    {
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_, &presentSupport);

        if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presentSupport)
        {
            return i;
        }
    }

    return UINT32_MAX;
}

// Enumerates all GPUs and selects the highest scoring suitable device.
void VulkanContext::pickPhysicalDevice()
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr);

    if (deviceCount == 0)
    {
        std::cerr << "No Vulkan-capable GPUs found\n";
        return;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data());

    int bestScore = 0;
    for (VkPhysicalDevice device : devices)
    {
        int score = scoreDevice(device);
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(device, &props);
        std::cout << "GPU: " << props.deviceName << " | Score: " << score << "\n";

        if (score > bestScore)
        {
            bestScore = score;
            physicalDevice_ = device;
        }
    }

    if (physicalDevice_ == VK_NULL_HANDLE)
    {
        std::cerr << "No suitable GPU found\n";
        return;
    }

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physicalDevice_, &props);
    std::cout << "Selected GPU: " << props.deviceName << "\n";

    graphicsQueueFamily_ = findQueueFamily(physicalDevice_);
}

// Creates the logical device with Vulkan 1.2 and 1.3 features enabled.
void VulkanContext::createLogicalDevice()
{
    float queuePriority = 1.0f;

    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = graphicsQueueFamily_;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &queuePriority;

    // Enable the Vulkan 1.2 and 1.3 features used throughout Phases 2 to 6.
    // These are chained via pNext so the driver sees them all in one call.
    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.bufferDeviceAddress = VK_TRUE;
    features12.descriptorIndexing = VK_TRUE;
    features12.runtimeDescriptorArray = VK_TRUE;
    features12.descriptorBindingPartiallyBound = VK_TRUE;
    features12.descriptorBindingVariableDescriptorCount = VK_TRUE;
    features12.timelineSemaphore = VK_TRUE;

    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;
    features13.maintenance4 = VK_TRUE;
    features13.pNext = &features12;

    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.pNext = &features13;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions_.size());
    deviceInfo.ppEnabledExtensionNames = deviceExtensions_.data();

    if (vkCreateDevice(physicalDevice_, &deviceInfo, nullptr, &device_) != VK_SUCCESS)
    {
        std::cerr << "Failed to create logical device\n";
        return;
    }

    vkGetDeviceQueue(device_, graphicsQueueFamily_, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, graphicsQueueFamily_, 0, &presentQueue_);
}