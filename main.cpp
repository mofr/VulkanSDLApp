#include <iostream>
#include <fstream>
#include <map>
#include <chrono>
#include <SDL.h>
#include <SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_metal.h>

#include <glm/glm.hpp>
#include <glm/ext.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_vulkan.h>

#include "Pipeline.h"
#include "Vertex.h"
#include "VulkanFunctions.h"
#include "Model.h"
#include "Camera.h"
#include "ObjFile.h"
#include "MeshObject.h"
#include "MeshFunctions.h"
#include "OrbitCameraController.h"
#include "FlyingCameraController.h"
#include "Swapchain.h"


VkDescriptorSet transferMaterialToGpu(Material const& material, Pipeline& pipeline, VkSampler sampler, VkImageView textureImageView) {
    Pipeline::MaterialProps materialProps{
        .diffuseFactor = material.diffuseFactor,
        .emitFactor = material.emitFactor,
        .specularHardness = material.specularHardness,
        .specularPower = material.specularPower,
    };
    return pipeline.createMaterial(textureImageView, sampler, materialProps);
}

MeshObject transferModelToGpu(VkPhysicalDevice physicalDevice, VkDevice device, VkCommandPool commandPool, VkQueue queue, VkSampler textureSampler, Pipeline& pipeline, const Model& model) {
    MeshObject object{};
    object.vertexBuffer = createVertexBuffer(physicalDevice, device, model.vertices);
    object.vertexCount = model.vertices.size();
    if (model.material.diffuseTexture.empty()) {
        ImageData whitePixel;
        uint8_t * whitePixelData = (uint8_t*) malloc(4);
        whitePixelData[0] = 255;
        whitePixelData[1] = 255;
        whitePixelData[2] = 255;
        whitePixelData[3] = 255;
        whitePixel.data.reset((void*) whitePixelData);
        whitePixel.imageFormat = VK_FORMAT_R8G8B8A8_UNORM;
        whitePixel.dataSize = 4;
        whitePixel.width = 1;
        whitePixel.height = 1;
        object.textureImage = createTextureImage(physicalDevice, device, commandPool, queue, whitePixel);
    }
    else {
        object.textureImage = createTextureImage(physicalDevice, device, commandPool, queue, model.material.diffuseTexture);
    }
    object.textureImageView = createImageView(device, object.textureImage, VK_FORMAT_R8G8B8A8_SRGB);
    object.material = model.material;
    object.materialDescriptorSet = transferMaterialToGpu(model.material, pipeline, textureSampler, object.textureImageView);
    return object;
}

static void check_vk_result(VkResult err)
{
    if (err == 0)
        return;
    fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
    if (err < 0)
        abort();
}

int main() {
    uint32_t width = 1024;
    uint32_t height = 768;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "Failed to initialize SDL: " << SDL_GetError() << std::endl;
        return -1;
    }

    SDL_Window* window = SDL_CreateWindow("Vulkan SDL App", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, SDL_WINDOW_VULKAN);
    if (!window) {
        std::cerr << "Failed to create window: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return -1;
    }

    std::vector extensions = {
        VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
        VK_EXT_METAL_SURFACE_EXTENSION_NAME,
        VK_KHR_SURFACE_EXTENSION_NAME,
        "VK_KHR_get_physical_device_properties2",
    };

    const std::vector validationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };
    if (!checkValidationLayerSupport(validationLayers)) {
        throw std::runtime_error("Validation layers requested, but not available!");
    }

    VkInstance instance;
    {
        VkInstanceCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR,
            .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
            .ppEnabledExtensionNames = extensions.data(),
            .enabledLayerCount = static_cast<uint32_t>(validationLayers.size()),
            .ppEnabledLayerNames = validationLayers.data(),
        };

        VkResult result = vkCreateInstance(&createInfo, nullptr, &instance);
        if (result != VK_SUCCESS) {
            std::cerr << "Failed to create Vulkan instance! " << result << std::endl;
            SDL_DestroyWindow(window);
            SDL_Quit();
            return -1;
        }
    }

    VkSurfaceKHR surface;
    if (SDL_Vulkan_CreateSurface(window, instance, &surface) != SDL_TRUE) {
        std::cerr << "Failed to create Vulkan surface: " << SDL_GetError() << std::endl;
        return -1;
    }

    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    for (const auto& device : devices) {
        VkPhysicalDeviceFeatures supportedFeatures;
        supportedFeatures.samplerAnisotropy = VK_TRUE;
        vkGetPhysicalDeviceFeatures(device, &supportedFeatures);
        if (!supportedFeatures.samplerAnisotropy) {
            std::cout << "Sampler anisotropy is not supported!" << std::endl;
        }
        physicalDevice = device;
        break;
    }

    if (physicalDevice == VK_NULL_HANDLE) {
        std::cerr << "Failed to create physical device!" << std::endl;
        return -1;
    }

    VkPhysicalDeviceProperties physicalDeviceProperties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &physicalDeviceProperties);

    VkDevice device;
    uint32_t graphicsQueueFamilyIndex = UINT32_MAX;
    {
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

        for (size_t i = 0; i < queueFamilies.size(); i++) {
            const auto& queueFamily = queueFamilies[i];

            if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                graphicsQueueFamilyIndex = i;
                break;
            }
        }

        if (graphicsQueueFamilyIndex == UINT32_MAX) {
            std::cerr << "Failed to find a graphics queue family!" << std::endl;
            return -1;
        }

        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = graphicsQueueFamilyIndex;
        queueCreateInfo.queueCount = 1;

        float queuePriority = 1.0f;
        queueCreateInfo.pQueuePriorities = &queuePriority;

        std::vector deviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            "VK_KHR_portability_subset",
        };
        VkPhysicalDeviceFeatures deviceFeatures{};
        deviceFeatures.samplerAnisotropy = VK_TRUE;
        VkDeviceCreateInfo deviceCreateInfo{};
        deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceCreateInfo.queueCreateInfoCount = 1;
        deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
        deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
        deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();
        deviceCreateInfo.pEnabledFeatures = &deviceFeatures;
        VkResult result = vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device);
        if (result != VK_SUCCESS) {
            std::cerr << "Failed to create Vulkan device! " << result << std::endl;
            SDL_DestroyWindow(window);
            SDL_Quit();
            return -1;
        }
    }

    VkQueue graphicsQueue;
    vkGetDeviceQueue(device, graphicsQueueFamilyIndex, 0, &graphicsQueue);
    if (graphicsQueue == VK_NULL_HANDLE) {
        std::cerr << "Failed to get graphics queue!" << std::endl;
        return -1;
    }

    VkQueue presentQueue;
    vkGetDeviceQueue(device, graphicsQueueFamilyIndex, 0, &presentQueue);
    if (presentQueue == VK_NULL_HANDLE) {
        std::cerr << "Failed to get present queue!" << std::endl;
        return -1;
    }

    uint32_t maxFramesInFlight = 3;
    Swapchain swapchain(physicalDevice, device, surface, {width, height}, maxFramesInFlight);

    VkCommandPool commandPool;
    {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = graphicsQueueFamilyIndex;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; // Enable reset command buffer
        if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
            std::cerr << "Failed to create command pool!" << std::endl;
            return -1;
        }
    }

    VkImage depthImage;
    VkDeviceMemory depthImageMemory;
    VkImageView depthImageView;
    VkFormat depthFormat = findDepthFormat(physicalDevice);
    {
        createImage(
            physicalDevice,
            device,
            width,
            height,
            depthFormat,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            depthImage,
            depthImageMemory
        );

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = depthImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = depthFormat;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &viewInfo, nullptr, &depthImageView) != VK_SUCCESS) {
            std::cerr << "Failed to create image views!" << std::endl;
            return -1;
        }
    }

    VkRenderPass renderPass;
    {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = swapchain.getFormat();
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; // Clear before use
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // Store after rendering
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentDescription depthAttachment{};
        depthAttachment.format = depthFormat;
        depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorAttachmentRef{
            .attachment = 0,
            .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        };
        VkAttachmentReference depthAttachmentRef{
            .attachment = 1,
            .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        };
        VkSubpassDescription subpass{
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachmentRef,
            .pDepthStencilAttachment = &depthAttachmentRef,
        };

        VkRenderPassCreateInfo renderPassCreateInfo{};
        renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        std::array attachments = {colorAttachment, depthAttachment};
        renderPassCreateInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        renderPassCreateInfo.pAttachments = attachments.data();
        renderPassCreateInfo.subpassCount = 1;
        renderPassCreateInfo.pSubpasses = &subpass;

        if (vkCreateRenderPass(device, &renderPassCreateInfo, nullptr, &renderPass) != VK_SUCCESS) {
            std::cerr << "Failed to create render pass!" << std::endl;
            return -1;
        }
    }

    std::vector<VkFramebuffer> framebuffers(swapchain.getImageCount());
    for (size_t i = 0; i < framebuffers.size(); i++) {
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        std::array attachments = {
            swapchain.getImageView(i),
            depthImageView,
        };
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = width;
        framebufferInfo.height = height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &framebuffers[i]) != VK_SUCCESS) {
            std::cerr << "Failed to create framebuffer!" << std::endl;
            return -1;
        }
    }

    std::vector<VkCommandBuffer> commandBuffers(maxFramesInFlight);
    {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; // Primary command buffers can be submitted to queues
        allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());
        if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
            std::cerr << "Failed to allocate command buffers!" << std::endl;
            return -1;
        }
    }

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    std::vector<VkSemaphore> renderFinishedSemaphores(maxFramesInFlight);
    std::vector<VkFence> renderFences(maxFramesInFlight);
    for (size_t i = 0; i < maxFramesInFlight; ++i) {
        vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]);
        vkCreateFence(device, &fenceInfo, nullptr, &renderFences[i]);
    }

    float maxAnisotropy = physicalDeviceProperties.limits.maxSamplerAnisotropy;
    VkSampler textureSampler = createTextureSampler(device, maxAnisotropy);

    Pipeline pipeline(physicalDevice, device, {width, height}, renderPass, 1024);
    std::vector<MeshObject> meshObjects;
    std::vector<Pipeline::Light> lights;

    {
        Model woodenStoolModel = loadObj("resources/wooden_stool_02_4k.obj");
        woodenStoolModel.material.specularHardness = 500;
        woodenStoolModel.material.specularPower = 5;
        MeshObject woodenStool = transferModelToGpu(physicalDevice, device, commandPool, graphicsQueue, textureSampler, pipeline, woodenStoolModel);
        meshObjects.push_back(woodenStool);
    }

    {
        Model lightModel1;
        lightModel1.vertices = createSphereMesh(2, 0.05);
        lightModel1.material.diffuseFactor = glm::vec3{0.0f};
        lightModel1.material.emitFactor = glm::vec3{1.0f, 0.5f, 0.5f};
        MeshObject lightObj1 = transferModelToGpu(physicalDevice, device, commandPool, graphicsQueue, textureSampler, pipeline, lightModel1);
        lightObj1.position = {-1.5f, 1.0f, 1.0f};
        meshObjects.push_back(lightObj1);
        lights.push_back(Pipeline::Light{.pos=lightObj1.position, .diffuseFactor={1.0f, 0.5f, 0.4f}});
    }

    {
        Model lightModel2;
        lightModel2.vertices = createSphereMesh(2, 0.05);
        lightModel2.material.diffuseFactor = glm::vec3{0.0f};
        lightModel2.material.emitFactor = glm::vec3{0.5f, 0.5f, 1.0f};
        MeshObject lightObj2 = transferModelToGpu(physicalDevice, device, commandPool, graphicsQueue, textureSampler, pipeline, lightModel2);
        lightObj2.position = {1.5f, 1.0f, 1.0f};
        meshObjects.push_back(lightObj2);
        lights.push_back(Pipeline::Light{.pos=lightObj2.position, .diffuseFactor={0.3f, 0.5f, 1.0f}});
    }

    {
        std::vector<Vertex> vertices;
        vertices.push_back({{-1.0f, 0, 1.0f}, {0, 1.0f, 0}, {1.0f, 1.0f, 1.0f}, {0, 0}});
        vertices.push_back({{1.0f, 0, 1.0f}, {0, 1.0f, 0}, {1.0f, 1.0f, 1.0f}, {1, 0}});
        vertices.push_back({{1.0f, 0, -1.0f}, {0, 1.0f, 0}, {1.0f, 1.0f, 1.0f}, {1, 1}});

        vertices.push_back({{1.0f, 0, -1.0f}, {0, 1.0f, 0}, {1.0f, 1.0f, 1.0f}, {1, 1}});
        vertices.push_back({{-1.0f, 0, -1.0f}, {0, 1.0f, 0}, {1.0f, 1.0f, 1.0f}, {0, 1}});
        vertices.push_back({{-1.0f, 0, 1.0f}, {0, 1.0f, 0}, {1.0f, 1.0f, 1.0f}, {0, 0}});

        Material floorMaterial{.specularHardness=50, .specularPower=1, .diffuseFactor = {0.5f, 0.5f, 0.5f}};
        Model model{vertices, floorMaterial};
        MeshObject floorObj;
        floorObj = transferModelToGpu(physicalDevice, device, commandPool, graphicsQueue, textureSampler, pipeline, model);
        meshObjects.push_back(floorObj);
    }

    Camera camera;
    camera.setAspectRatio(float(width) / float(height));
    camera.setPosition({0.0f, 1.0f, 2.0f});
    camera.lookAt({0, 0, 0});
    OrbitCameraController orbitCameraController(width, height, glm::vec3(0.0f, 3.0f, 5.0f));
    FlyingCameraController flyingCameraController;
    CameraController* cameraController = &flyingCameraController;
    SDL_SetRelativeMouseMode(SDL_TRUE);
    bool controlCamera = true;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    ImGui_ImplSDL2_InitForVulkan(window);
    ImGui_ImplVulkan_InitInfo init_info {
        .Instance = instance,
        .PhysicalDevice = physicalDevice,
        .Device = device,
        .QueueFamily = graphicsQueueFamilyIndex,
        .Queue = graphicsQueue,
        .PipelineCache = nullptr,
        .DescriptorPoolSize = 2,
        .RenderPass = renderPass,
        .Subpass = 0,
        .MinImageCount = 2,
        .ImageCount = 2,
        .MSAASamples = VK_SAMPLE_COUNT_1_BIT,
        .Allocator = nullptr,
        .CheckVkResultFn = check_vk_result,
    };
    ImGui_ImplVulkan_Init(&init_info);

    typedef std::chrono::steady_clock Clock;
    auto lastUpdateTime = Clock::now();
    size_t currentFrame = 0;
    bool running = true;
    SDL_Event event;
    while (running) {
        vkWaitForFences(device, 1, &renderFences[currentFrame], true, UINT64_MAX);
        auto [swapchainImageIndex, imageAvailableSemaphore] = swapchain.acquireNextImage();
        vkResetFences(device, 1, &renderFences[currentFrame]);

        static const float maxFrameTime = 1.0f / 30.0f;
        auto now = Clock::now();
        float dt = std::chrono::duration<float>(now - lastUpdateTime).count();
        dt = glm::min(dt, maxFrameTime);
        lastUpdateTime = now;
        auto fps = static_cast<int>(1.0f / dt);

        // record commands
        VkCommandBuffer commandBuffer = commandBuffers[currentFrame];
        vkResetCommandBuffer(commandBuffer, 0);
        VkCommandBufferBeginInfo commanBufferBeginInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        };
        if (vkBeginCommandBuffer(commandBuffer, &commanBufferBeginInfo) != VK_SUCCESS) {
            std::cerr << "Failed to begin recording command buffer!" << std::endl;
            return -1;
        }

        std::array clearValues{
            VkClearValue{.color={{0.05f, 0.05f, 0.05f, 1.0f}}},
            VkClearValue{.depthStencil={1.0f, 0}}
        };
        VkRenderPassBeginInfo renderPassBeginInfo{
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = renderPass,
            .framebuffer = framebuffers[currentFrame],
            .renderArea.offset = { 0, 0 },
            .renderArea.extent = swapchain.getExtent(),
            .clearValueCount = clearValues.size(),
            .pClearValues = clearValues.data(),
        };
        vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

        pipeline.draw(
            commandBuffer,
            camera.getProjectionMatrix(),
            camera.getViewMatrix(),
            meshObjects,
            lights
        );

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        ImGui::Begin("Config");
        std::string frameTimeString = std::to_string(dt * 1000) + " ms";
        std::string fpsString = std::to_string(fps) + " FPS";
        ImGui::Text(frameTimeString.c_str());
        ImGui::Text(fpsString.c_str());
        {
            static const std::array labels = { "Trilinear", "2X", "4X", "8X", "16X" };
            static const std::array values = { 0.0f, 2.0f, 4.0f, 8.0f, 16.0f };
            static int elem = values.size() - 1;
            if (ImGui::SliderInt("Anisotropy", &elem, 0, values.size() - 1, labels[elem])) {
                maxAnisotropy = values[elem];
                textureSampler = createTextureSampler(device, maxAnisotropy);
                for (auto& obj : meshObjects) {
                    obj.materialDescriptorSet = transferMaterialToGpu(obj.material, pipeline, textureSampler, obj.textureImageView);
                }
            }
        }
        {
            VkSampleCountFlags counts = physicalDeviceProperties.limits.framebufferColorSampleCounts & physicalDeviceProperties.limits.framebufferDepthSampleCounts;
            // if (counts & VK_SAMPLE_COUNT_64_BIT) { return VK_SAMPLE_COUNT_64_BIT; }
            // if (counts & VK_SAMPLE_COUNT_32_BIT) { return VK_SAMPLE_COUNT_32_BIT; }
            // if (counts & VK_SAMPLE_COUNT_16_BIT) { return VK_SAMPLE_COUNT_16_BIT; }
            // if (counts & VK_SAMPLE_COUNT_8_BIT) { return VK_SAMPLE_COUNT_8_BIT; }
            // if (counts & VK_SAMPLE_COUNT_4_BIT) { return VK_SAMPLE_COUNT_4_BIT; }
            // if (counts & VK_SAMPLE_COUNT_2_BIT) { return VK_SAMPLE_COUNT_2_BIT; }
            static const std::array labels = { "Off", "2", "4", "8", "16", "32", "64" };
            static const std::array values = { 0.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f };
            static int elem = 0;
            static int elemCount = values.size();
            ImGui::SliderInt("Multisampling", &elem, 0, elemCount - 1, labels[elem]);
        }
        ImGui::End();
        ImGui::Render();
        ImDrawData* imguiDrawData = ImGui::GetDrawData();
        ImGui_ImplVulkan_RenderDrawData(imguiDrawData, commandBuffer);

        vkCmdEndRenderPass(commandBuffer);

        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
            std::cerr << "Failed to record command buffer!" << std::endl;
            return -1;
        }

        // Submit the command buffer
        VkPipelineStageFlags waitStages[] = {
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT // Wait for color output stage
        };
        VkSubmitInfo submitInfo{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &imageAvailableSemaphore, // Wait for the image to be available
            .pWaitDstStageMask = waitStages,
            .commandBufferCount = 1,
            .pCommandBuffers = &commandBuffers[currentFrame], // Command buffer for this image
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &renderFinishedSemaphores[currentFrame], // Signal when rendering is finished
        };
        if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, renderFences[currentFrame]) != VK_SUCCESS) {
            std::cerr << "Failed to submit draw command buffer!" << std::endl;
            return -1;
        }

        // Present the rendered image
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &renderFinishedSemaphores[currentFrame]; // Wait for rendering to finish
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapchain.getHandle();
        presentInfo.pImageIndices = &swapchainImageIndex;

        if (vkQueuePresentKHR(presentQueue, &presentInfo) != VK_SUCCESS) {
            std::cerr << "Failed to present swapchain image!" << std::endl;
        }

        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                running = false;
            }
            if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    running = false;
                }
                if (event.key.keysym.sym == SDLK_1) {
                    cameraController = &flyingCameraController;
                }
                if (event.key.keysym.sym == SDLK_2) {
                    cameraController = &orbitCameraController;
                }
                if (event.key.keysym.sym == SDLK_RETURN) {
                    SDL_SetRelativeMouseMode((SDL_bool)(SDL_GetRelativeMouseMode() ^ 1));
                    controlCamera = !controlCamera;
                }
            }
            if (controlCamera)
                cameraController->update(camera, event, dt);
        }
        if (controlCamera)
            cameraController->update(camera, dt);

        currentFrame = (currentFrame + 1) % maxFramesInFlight;
    }

    // Cleanup
    {
        VkResult err = vkDeviceWaitIdle(device);
        check_vk_result(err);
    }
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    // First need to delete all child objects
    // vkDestroyInstance(instance, nullptr);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
