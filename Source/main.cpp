#include "Core/VulkanContext.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <fstream>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <Renderer/Swapchain.hpp>

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

// Records draw commands into the command buffer for a single frame.
// This runs every frame: begin render pass, bind pipeline, draw, end.
static void recordCommandBuffer(VkCommandBuffer commandBuffer, VkRenderPass renderPass,
    VkFramebuffer framebuffer, VkPipeline pipeline, VkExtent2D extent)
{
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
    {
        std::cerr << "Failed to begin command buffer\n";
        return;
    }

    VkClearValue clearColor{};
    clearColor.color = { 0.0f, 0.0f, 0.0f, 1.0f };

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = framebuffer;
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = extent;
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    // Viewport and scissor are dynamic so we set them here at record time.
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = extent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    // 3 vertices, 1 instance, no offsets. Positions come from the vertex shader.
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
    {
        std::cerr << "Failed to end command buffer\n";
    }
}

// Creates one framebuffer per swapchain image view, bound to the render pass.
// A framebuffer connects a render pass to specific image views to render into.
static std::vector<VkFramebuffer> createFramebuffers(VkDevice device,
    VkRenderPass renderPass, const std::vector<VkImageView>& imageViews, VkExtent2D extent)
{
    std::vector<VkFramebuffer> framebuffers(imageViews.size());

    for (size_t i = 0; i < imageViews.size(); i++)
    {
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = &imageViews[i];
        framebufferInfo.width = extent.width;
        framebufferInfo.height = extent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &framebuffers[i]) != VK_SUCCESS)
        {
            std::cerr << "Failed to create framebuffer " << i << "\n";
        }
    }

    return framebuffers;
}

// Creates the full graphics pipeline by explicitly filling every sub-struct.
// Vulkan requires all state to be declared upfront unlike OpenGL's implicit state machine.
static VkPipeline createGraphicsPipeline(VkDevice device, VkRenderPass renderPass,
    VkExtent2D extent, VkShaderModule vertShader, VkShaderModule fragShader,
    VkPipelineLayout& outLayout)
{
    // Shader stages: which shader runs at which pipeline stage.
    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertShader;
    vertStage.pName = "VSMain";

    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragShader;
    fragStage.pName = "PSMain";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertStage, fragStage };

    // No vertex buffer yet. Positions are hardcoded in the vertex shader.
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    // Draw a list of triangles with no index restart.
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Viewport and scissor are dynamic so we can resize without rebuilding the pipeline.
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // No blending. Write all color channels.
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &blendAttachment;

    // Dynamic state lets us set viewport and scissor at draw time instead of baking them in.
    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    // Empty pipeline layout for now. Push constants and descriptor sets added in Phase 3.
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &outLayout) != VK_SUCCESS)
    {
        std::cerr << "Failed to create pipeline layout\n";
        return VK_NULL_HANDLE;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = outLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS)
    {
        std::cerr << "Failed to create graphics pipeline\n";
    }

    return pipeline;
}

// Reads a SPIR-V binary from disk and wraps it in a VkShaderModule.
// The module is just a thin container around the bytecode. The driver
// compiles it to GPU machine code when the pipeline is created.
static VkShaderModule loadShaderModule(VkDevice device, const std::string& path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open())
    {
        std::cerr << "Failed to open shader: " << path << "\n";
        return VK_NULL_HANDLE;
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = buffer.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(buffer.data());

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
    {
        std::cerr << "Failed to create shader module: " << path << "\n";
    }

    return shaderModule;
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

    VulkanContext context(window);
    VkInstance       instance = context.Instance();
    VkPhysicalDevice physicalDevice = context.PhysicalDevice();
    VkDevice         device = context.Device();
    VkQueue          graphicsQueue = context.GraphicsQueue();
    uint32_t         queueFamilyIndex = context.GraphicsQueueFamilyIndex();
    VkSurfaceKHR     surface = context.Surface();

    VkPhysicalDeviceProperties gpuProperties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &gpuProperties);

    Swapchain swapchain(physicalDevice, device, surface, window);

    VkFormat             swapchainFormat = swapchain.Format();
    VkExtent2D           swapchainExtent = swapchain.Extent();
    const std::vector<VkImage>& swapchainImages = swapchain.Images();

    VkRenderPass renderPass = createRenderPass(device, swapchainFormat);
    if (renderPass == VK_NULL_HANDLE)
    {
        return 1;
    }

    VkShaderModule vertShader = loadShaderModule(device, SHADER_DIR "triangle_vs.spv");
    VkShaderModule fragShader = loadShaderModule(device, SHADER_DIR "triangle_ps.spv");

    if (vertShader == VK_NULL_HANDLE || fragShader == VK_NULL_HANDLE)
    {
        return 1;
    }

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = createGraphicsPipeline(device, renderPass, swapchainExtent,
        vertShader, fragShader, pipelineLayout);
    if (pipeline == VK_NULL_HANDLE)
    {
        return 1;
    }

    std::vector<VkImageView> swapchainImageViews;
    for (uint32_t i = 0; i < swapchain.ImageCount(); i++)
    {
        swapchainImageViews.push_back(swapchain.ImageView(i));
    }

    std::vector<VkFramebuffer> framebuffers = createFramebuffers(device, renderPass,
        swapchainImageViews, swapchainExtent);

    // The command pool owns the memory for command buffers.
    // RESET_COMMAND_BUFFER_BIT lets us re-record a buffer without resetting the whole pool.
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndex;

    VkCommandPool commandPool = VK_NULL_HANDLE;
    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS)
    {
        std::cerr << "Failed to create command pool\n";
        return 1;
    }

    // One command buffer per frame in flight. 2 lets the CPU record frame N+1
    // while the GPU is still executing frame N.
    constexpr int MAX_FRAMES_IN_FLIGHT = 2;
    std::vector<VkCommandBuffer> commandBuffers(MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = MAX_FRAMES_IN_FLIGHT;

    if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS)
    {
        std::cerr << "Failed to allocate command buffers\n";
        return 1;
    }

    // ImGui setup
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForVulkan(window, true);

    // ImGui needs its own descriptor pool to allocate its font texture descriptor.
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolCreateInfo{};
    poolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCreateInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolCreateInfo.maxSets = 1;
    poolCreateInfo.poolSizeCount = 1;
    poolCreateInfo.pPoolSizes = &poolSize;

    VkDescriptorPool imguiPool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(device, &poolCreateInfo, nullptr, &imguiPool) != VK_SUCCESS)
    {
        std::cerr << "Failed to create ImGui descriptor pool\n";
        return 1;
    }

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance = instance;
    initInfo.PhysicalDevice = physicalDevice;
    initInfo.Device = device;
    initInfo.QueueFamily = queueFamilyIndex;
    initInfo.Queue = graphicsQueue;
    initInfo.DescriptorPool = imguiPool;
    initInfo.MinImageCount = 2;
    initInfo.ImageCount = swapchain.ImageCount();

    initInfo.PipelineInfoMain.RenderPass = renderPass;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.PipelineInfoMain.Subpass = 0;

    ImGui_ImplVulkan_Init(&initInfo);

    // Per frame: one semaphore signals when the swapchain image is ready to render into,
    // one signals when rendering is done and the image can be presented,
    // one fence lets the CPU wait until the GPU has finished this frame slot.
    std::vector<VkSemaphore> imageAvailableSemaphores(swapchainImages.size());
    std::vector<VkSemaphore> renderFinishedSemaphores(MAX_FRAMES_IN_FLIGHT);
    std::vector<VkFence> inFlightFences(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    // Start the fence signaled so the first vkWaitForFences call returns immediately.
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < imageAvailableSemaphores.size(); i++)
    {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS)
        {
            std::cerr << "Failed to create image available semaphore " << i << "\n";
            return 1;
        }
    }

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS)
        {
            std::cerr << "Failed to create sync objects for frame " << i << "\n";
            return 1;
        }
    }

    uint32_t currentFrame = 0;
    uint32_t semaphoreIndex = 0;

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        // Wait for the previous use of this frame slot to finish before reusing it.
        vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

        uint32_t imageIndex = 0;
        vkAcquireNextImageKHR(device, swapchain.Handle(), UINT64_MAX,
            imageAvailableSemaphores[semaphoreIndex], VK_NULL_HANDLE, &imageIndex);

        // Only reset the fence after a successful acquire to avoid a deadlock
        // if the swapchain returns OUT_OF_DATE.
        vkResetFences(device, 1, &inFlightFences[currentFrame]);

        vkResetCommandBuffer(commandBuffers[currentFrame], 0);

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Debug");
        ImGui::Text("GPU: %s", gpuProperties.deviceName);
        ImGui::Text("Frame time: %.3f ms", 1000.0f / ImGui::GetIO().Framerate);
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::End();

        ImGui::Render();

        recordCommandBuffer(commandBuffers[currentFrame], renderPass,
            framebuffers[imageIndex], pipeline, swapchainExtent);

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &imageAvailableSemaphores[semaphoreIndex];
        submitInfo.pWaitDstStageMask = &waitStage;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffers[currentFrame];
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &renderFinishedSemaphores[currentFrame];

        if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]) != VK_SUCCESS)
        {
            std::cerr << "Failed to submit draw command buffer\n";
            return 1;
        }

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &renderFinishedSemaphores[currentFrame];
        presentInfo.swapchainCount = 1;
        VkSwapchainKHR swapchainHandle = swapchain.Handle();
        presentInfo.pSwapchains = &swapchainHandle;
        presentInfo.pImageIndices = &imageIndex;

        vkQueuePresentKHR(graphicsQueue, &presentInfo);

        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;

        semaphoreIndex = (semaphoreIndex + 1) % static_cast<uint32_t>(imageAvailableSemaphores.size());
    }

    // Wait for the GPU to finish all work before destroying anything.
    vkDeviceWaitIdle(device);

    for (size_t i = 0; i < imageAvailableSemaphores.size(); i++)
    {
        vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
    }
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
        vkDestroyFence(device, inFlightFences[i], nullptr);
    }

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    vkDestroyDescriptorPool(device, imguiPool, nullptr);
    vkDestroyCommandPool(device, commandPool, nullptr);

    for (VkFramebuffer fb : framebuffers)
    {
        vkDestroyFramebuffer(device, fb, nullptr);
    }

    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);

    vkDestroyShaderModule(device, fragShader, nullptr);
    vkDestroyShaderModule(device, vertShader, nullptr);

    vkDestroyRenderPass(device, renderPass, nullptr);
    
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}