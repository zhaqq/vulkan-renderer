#include <iostream>
#include <vector>
#include <string>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#ifdef NDEBUG
constexpr bool enableValidation = false;
#else
constexpr bool enableValidation = true;
#endif

const std::vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

const std::vector<const char*> deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

// Prints Vulkan validation messages to stderr.
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData)
{
    std::cerr << "[Vulkan] " << pCallbackData->pMessage << "\n";
    return VK_FALSE;
}

VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;

// Registers the debug messenger with the Vulkan instance so validation messages are routed to debugCallback.
static void createDebugMessenger(VkInstance instance)
{
    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;

    // vkCreateDebugUtilsMessengerEXT is an extension function, not statically linked.
    // We look it up at runtime via the instance.
    if (auto func = (PFN_vkCreateDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"))
    {
        if (func(instance, &createInfo, nullptr, &debugMessenger) != VK_SUCCESS)
        {
            std::cerr << "Failed to create debug messenger\n";
        }
    }
    else
    {
        std::cerr << "vkCreateDebugUtilsMessengerEXT not found\n";
    }
}

// Unregisters and destroys the debug messenger. Must be called before vkDestroyInstance.
static void destroyDebugMessenger(VkInstance instance)
{
    if (auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"))
    {
        func(instance, debugMessenger, nullptr);
    }
}

// Returns true if the physical device supports everything this renderer requires.
static bool isDeviceSuitable(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    // Check Vulkan 1.2 features we need in Phase 2.
    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;

    // Check Vulkan 1.3 features. dynamicRendering and synchronization2 replace
    // VkRenderPass and legacy barriers in Phase 2.
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

    // Check swapchain extension support.
    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> available(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, available.data());

    for (const char* required : deviceExtensions)
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

// Scores a physical device. Higher is better. Returns 0 if the device is unsuitable.
static int scoreDevice(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    if (!isDeviceSuitable(device, surface))
    {
        return 0;
    }

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(device, &props);

    int score = 0;

    // Discrete GPUs are strongly preferred over integrated.
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

// Enumerates all GPUs and selects the best one based on feature support and device type.
static VkPhysicalDevice pickPhysicalDevice(VkInstance instance, VkSurfaceKHR surface)
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

    if (deviceCount == 0)
    {
        std::cerr << "No Vulkan-capable GPUs found\n";
        return VK_NULL_HANDLE;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    VkPhysicalDevice best = VK_NULL_HANDLE;
    int bestScore = 0;

    for (VkPhysicalDevice device : devices)
    {
        int score = scoreDevice(device, surface);
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(device, &props);
        std::cout << "GPU: " << props.deviceName << " | Score: " << score << "\n";

        if (score > bestScore)
        {
            bestScore = score;
            best = device;
        }
    }

    if (best == VK_NULL_HANDLE)
    {
        std::cerr << "No suitable GPU found\n";
        return VK_NULL_HANDLE;
    }

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(best, &props);
    std::cout << "Selected GPU: " << props.deviceName << "\n";

    return best;
}

// Finds the queue family index that supports both graphics and present.
static uint32_t findQueueFamily(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());

    for (uint32_t i = 0; i < count; i++)
    {
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);

        if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presentSupport)
        {
            return i;
        }
    }

    return UINT32_MAX;
}

// Creates the logical device and retrieves the graphics/present queue handle.
static VkDevice createLogicalDevice(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex)
{
    float queuePriority = 1.0f;

    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = queueFamilyIndex;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &queuePriority;

    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    deviceInfo.ppEnabledExtensionNames = deviceExtensions.data();

    VkDevice device = VK_NULL_HANDLE;
    if (vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device) != VK_SUCCESS)
    {
        std::cerr << "Failed to create logical device\n";
    }

    return device;
}

int main()
{
    if (!glfwInit())
    {
        std::cerr << "Failed to initialize GLFW\n";
        return 1;
    }

    // GLFW_NO_API prevents GLFW from creating an OpenGL context.
    // Vulkan manages its own presentation; an OpenGL context would conflict.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Vulkan Renderer", nullptr, nullptr);

    // GLFW knows which platform-specific surface extensions Vulkan needs
    // (e.g. VK_KHR_win32_surface on Windows) and returns them here.
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
        instanceInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        instanceInfo.ppEnabledLayerNames = validationLayers.data();
    }

    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&instanceInfo, nullptr, &instance) != VK_SUCCESS)
    {
        std::cerr << "Failed to create Vulkan instance\n";
        return 1;
    }

    if (enableValidation)
    {
        createDebugMessenger(instance);
    }

    // The surface connects Vulkan to the OS window. It must be created before
    // physical device selection because surface support affects which GPU we pick.
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS)
    {
        std::cerr << "Failed to create window surface\n";
        return 1;
    }

    VkPhysicalDevice physicalDevice = pickPhysicalDevice(instance, surface);
    if (physicalDevice == VK_NULL_HANDLE)
    {
        return 1;
    }

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
    }

    // Destroy in reverse creation order. Vulkan does not clean up after you.
    vkDestroySurfaceKHR(instance, surface, nullptr);

    if (enableValidation)
    {
        destroyDebugMessenger(instance);
    }

    vkDestroyInstance(instance, nullptr);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}