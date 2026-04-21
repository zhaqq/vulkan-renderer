#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
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

// Queries surface capabilities and selects optimal swapchain settings.
static VkSwapchainKHR createSwapchain(VkPhysicalDevice physicalDevice, VkDevice device,
    VkSurfaceKHR surface, uint32_t queueFamilyIndex, GLFWwindow* window,
    VkFormat& outFormat, VkExtent2D& outExtent)
{
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &caps);

    // Choose surface format. SRGB is preferred for correct gamma handling.
    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());

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
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, presentModes.data());

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
    VkExtent2D extent{};
    if (caps.currentExtent.width != UINT32_MAX)
    {
        extent = caps.currentExtent;
    }
    else
    {
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        extent.width = std::clamp(static_cast<uint32_t>(w), caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = std::clamp(static_cast<uint32_t>(h), caps.minImageExtent.height, caps.maxImageExtent.height);
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
    swapchainInfo.surface = surface;
    swapchainInfo.minImageCount = imageCount;
    swapchainInfo.imageFormat = chosenFormat.format;
    swapchainInfo.imageColorSpace = chosenFormat.colorSpace;
    swapchainInfo.imageExtent = extent;
    swapchainInfo.imageArrayLayers = 1;
    swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainInfo.preTransform = caps.currentTransform;
    swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainInfo.presentMode = chosenPresentMode;
    swapchainInfo.clipped = VK_TRUE;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    if (vkCreateSwapchainKHR(device, &swapchainInfo, nullptr, &swapchain) != VK_SUCCESS)
    {
        std::cerr << "Failed to create swapchain\n";
    }

    outFormat = chosenFormat.format;
    outExtent = extent;
    return swapchain;
}

// Creates one VkImageView per swapchain image. Views describe how to interpret the raw image memory.
static std::vector<VkImageView> createImageViews(VkDevice device,
    const std::vector<VkImage>& images, VkFormat format)
{
    std::vector<VkImageView> imageViews(images.size());

    for (size_t i = 0; i < images.size(); i++)
    {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = images[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
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

        if (vkCreateImageView(device, &viewInfo, nullptr, &imageViews[i]) != VK_SUCCESS)
        {
            std::cerr << "Failed to create image view " << i << "\n";
        }
    }

    return imageViews;
}

// Creates a legacy render pass with a single color attachment.
// Phase 2 replaces this entirely with vkCmdBeginRenderingKHR (dynamic rendering).
// We build it here to understand what dynamic rendering eliminates.
static VkRenderPass createRenderPass(VkDevice device, VkFormat swapchainFormat)
{
    // The attachment describes the swapchain image: clear on load, store on done,
    // and transition from UNDEFINED to PRESENT_SRC_KHR.
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapchainFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    // The subpass dependency tells the driver to wait for the swapchain image
    // to be available before writing color output. Without this the render pass
    // could start before the image is ready to be written to.
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    VkRenderPass renderPass = VK_NULL_HANDLE;
    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS)
    {
        std::cerr << "Failed to create render pass\n";
    }

    return renderPass;
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

    uint32_t queueFamilyIndex = findQueueFamily(physicalDevice, surface);
    if (queueFamilyIndex == UINT32_MAX)
    {
        std::cerr << "No suitable queue family found\n";
        return 1;
    }

    VkDevice device = createLogicalDevice(physicalDevice, queueFamilyIndex);
    if (device == VK_NULL_HANDLE)
    {
        return 1;
    }

    // VkPhysicalDevice is the hardware. VkDevice is the logical connection to it
    // and owns all resources created from this point forward.
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, queueFamilyIndex, 0, &graphicsQueue);

    VkFormat swapchainFormat{};
    VkExtent2D swapchainExtent{};
    VkSwapchainKHR swapchain = createSwapchain(physicalDevice, device, surface,
        queueFamilyIndex, window, swapchainFormat, swapchainExtent);
    if (swapchain == VK_NULL_HANDLE)
    {
        return 1;
    }

    // Retrieve swapchain image handles. Vulkan may allocate more than we requested.
    uint32_t imageCount = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
    std::vector<VkImage> swapchainImages(imageCount);
    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data());

    std::cout << "Swapchain created: " << swapchainExtent.width << "x"
        << swapchainExtent.height << ", " << imageCount << " images\n";

    std::vector<VkImageView> swapchainImageViews = createImageViews(device, swapchainImages, swapchainFormat);

    VkRenderPass renderPass = createRenderPass(device, swapchainFormat);
    if (renderPass == VK_NULL_HANDLE)
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

    vkDestroyRenderPass(device, renderPass, nullptr);

    for (VkImageView view : swapchainImageViews)
    {
        vkDestroyImageView(device, view, nullptr);
    }

    // Destroy in reverse creation order. Vulkan does not clean up after you.
    vkDestroySwapchainKHR(device, swapchain, nullptr);
    vkDestroyDevice(device, nullptr);
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