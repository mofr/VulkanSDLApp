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
#include "RenderSurface.h"


VkDescriptorSet transferMaterialToGpu(Material const& material, Pipeline& pipeline, VkSampler sampler, VkImageView textureImageView) {
    Pipeline::MaterialProps materialProps{
        .diffuseFactor = material.diffuseFactor,
        .emitFactor = material.emitFactor,
        .specularHardness = material.specularHardness,
        .specularPower = material.specularPower,
    };
    return pipeline.createMaterial(textureImageView, sampler, materialProps);
}

MeshObject transferModelToGpu(VkPhysicalDevice physicalDevice, VkDevice device, VkCommandPool commandPool, VkQueue queue, float maxAnisotropy, Pipeline& pipeline, const Model& model) {
    MeshObject object{};
    object.vertexBuffer = createVertexBuffer(physicalDevice, device, model.vertices);
    object.vertexCount = model.vertices.size();
    ImageData imageData;
    if (model.material.diffuseTexture.empty()) {
        uint8_t * whitePixelData = (uint8_t*) malloc(4);
        whitePixelData[0] = 255;
        whitePixelData[1] = 255;
        whitePixelData[2] = 255;
        whitePixelData[3] = 255;
        imageData.data.reset((void*) whitePixelData);
        imageData.imageFormat = VK_FORMAT_R8G8B8A8_UNORM;
        imageData.dataSize = 4;
        imageData.width = 1;
        imageData.height = 1;
    }
    else {
        imageData = loadImage(model.material.diffuseTexture);
    }
    object.mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(imageData.width, imageData.height)))) + 1;
    object.textureImage = createTextureImage(physicalDevice, device, commandPool, queue, imageData, object.mipLevels);
    object.textureImageView = createImageView(device, object.textureImage, VK_FORMAT_R8G8B8A8_SRGB, object.mipLevels);
    object.material = model.material;
    object.textureSampler = createTextureSampler(device, maxAnisotropy, object.mipLevels);
    object.materialDescriptorSet = transferMaterialToGpu(model.material, pipeline, object.textureSampler, object.textureImageView);
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

struct RenderingConfig {
    bool vsyncEnabled = true;
    float maxAnisotropy = 0;
    VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;
    bool useMipMaps = true;
};

bool renderingConfigGui(RenderingConfig& config, float dt, VkPhysicalDeviceProperties const& physicalDeviceProperties) {
    auto fps = static_cast<int>(1.0f / dt);
    bool changed = false;

    ImGui::Begin("Config");
    std::string frameTimeString = std::to_string(dt * 1000) + " ms";
    std::string fpsString = std::to_string(fps) + " FPS";
    ImGui::Text("%s", frameTimeString.c_str());
    ImGui::Text("%s", fpsString.c_str());
    
    changed |= ImGui::Checkbox("VSync", &config.vsyncEnabled);
    changed |= ImGui::Checkbox("Use mipmaps", &config.useMipMaps);

    {
        static const std::array labels = { "Trilinear", "2X", "4X", "8X", "16X" };
        static const std::array values = { 0.0f, 2.0f, 4.0f, 8.0f, 16.0f };
        int count = values.size();
        int elem = 0;
        for (elem = 0; elem < count; ++elem) {
            if (config.maxAnisotropy == values[elem]) break;
        }
        if (ImGui::SliderInt("Anisotropy", &elem, 0, count - 1, labels[elem])) {
            changed = true;
            config.maxAnisotropy = values[elem];
        }
    }
    {
        static const std::array labels = { "Off", "2", "4", "8", "16", "32", "64" };
        static const std::array values = { 
            VK_SAMPLE_COUNT_1_BIT,
            VK_SAMPLE_COUNT_2_BIT,
            VK_SAMPLE_COUNT_4_BIT,
            VK_SAMPLE_COUNT_8_BIT,
            VK_SAMPLE_COUNT_16_BIT,
            VK_SAMPLE_COUNT_32_BIT,
            VK_SAMPLE_COUNT_64_BIT
        };
        int elemCount = 1;
        VkSampleCountFlags counts = physicalDeviceProperties.limits.framebufferColorSampleCounts & physicalDeviceProperties.limits.framebufferDepthSampleCounts;
        if (counts & VK_SAMPLE_COUNT_2_BIT) { elemCount = 2; }
        if (counts & VK_SAMPLE_COUNT_4_BIT) { elemCount = 3; }
        if (counts & VK_SAMPLE_COUNT_8_BIT) { elemCount = 4; }
        if (counts & VK_SAMPLE_COUNT_16_BIT) { elemCount = 5; }
        if (counts & VK_SAMPLE_COUNT_32_BIT) { elemCount = 6; }
        if (counts & VK_SAMPLE_COUNT_64_BIT) { elemCount = 7; }
        int elem = 0;
        for (elem = 0; elem < elemCount; ++elem) {
            if (config.msaaSamples == values[elem]) break;
        }
        if (ImGui::SliderInt("Multisampling", &elem, 0, elemCount - 1, labels[elem])) {
            changed = true;
            config.msaaSamples = values[elem];
        }
    }
    ImGui::End();

    return changed;
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
        // VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME,
        // "VK_KHR_get_surface_capabilities2",
    };

    const std::vector<const char*> validationLayers = {
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

    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    std::vector<VkPhysicalDevice> physicalDevices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, physicalDevices.data());

    for (const auto& device : physicalDevices) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(device, &props);
        static std::array typeStrings = {
            "Other",
            "Integrated GPU",
            "Discrete GPU",
            "Virtual GPU",
            "CPU"
        };
        std::cout << typeStrings[props.deviceType] << ": " << props.deviceName << std::endl;
    }

    for (const auto& device : physicalDevices) {
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
            // VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME,
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

    RenderingConfig config{
        .vsyncEnabled = true,
        .maxAnisotropy = physicalDeviceProperties.limits.maxSamplerAnisotropy,
        .msaaSamples = VK_SAMPLE_COUNT_4_BIT,
    };
    RenderingConfig stagingConfig = config;

    RenderSurface renderSurface({
        .instance = instance,
        .physicalDevice = physicalDevice,
        .device = device,
        .window = window,
        .graphicsQueue = graphicsQueue,
        .presentQueue = graphicsQueue,
        .graphicsQueueFamilyIndex = graphicsQueueFamilyIndex,
        .extent = {width, height},
        .framesInFlight = 3,
        .vsyncEnabled = config.vsyncEnabled,
        .msaaSamples = config.msaaSamples,
    });

    Pipeline pipeline(
        physicalDevice,
        device,
        renderSurface.getExtent(),
        renderSurface.getRenderPass(),
        renderSurface.getMsaaSamples(),
        1024
    );
    std::vector<MeshObject> meshObjects;
    std::vector<Pipeline::Light> lights;

    {
        Model woodenStoolModel = loadObj("resources/wooden_stool_02_4k.obj");
        woodenStoolModel.material.specularHardness = 500;
        woodenStoolModel.material.specularPower = 5;
        MeshObject woodenStool = transferModelToGpu(physicalDevice, device, commandPool, graphicsQueue, config.maxAnisotropy, pipeline, woodenStoolModel);
        meshObjects.push_back(woodenStool);
    }

    {
        Model lightModel1;
        lightModel1.vertices = createSphereMesh(2, 0.05);
        lightModel1.material.diffuseFactor = glm::vec3{0.0f};
        lightModel1.material.emitFactor = glm::vec3{1.0f, 0.5f, 0.5f};
        MeshObject lightObj1 = transferModelToGpu(physicalDevice, device, commandPool, graphicsQueue, config.maxAnisotropy, pipeline, lightModel1);
        lightObj1.position = {-1.5f, 1.0f, 1.0f};
        meshObjects.push_back(lightObj1);
        lights.push_back(Pipeline::Light{.pos=lightObj1.position, .diffuseFactor={1.0f, 0.5f, 0.4f}});
    }

    {
        Model lightModel2;
        lightModel2.vertices = createSphereMesh(2, 0.05);
        lightModel2.material.diffuseFactor = glm::vec3{0.0f};
        lightModel2.material.emitFactor = glm::vec3{0.5f, 0.5f, 1.0f};
        MeshObject lightObj2 = transferModelToGpu(physicalDevice, device, commandPool, graphicsQueue, config.maxAnisotropy, pipeline, lightModel2);
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
        floorObj = transferModelToGpu(physicalDevice, device, commandPool, graphicsQueue, config.maxAnisotropy, pipeline, model);
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
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    ImGui_ImplSDL2_InitForVulkan(window);
    ImGui_ImplVulkan_InitInfo init_info {
        .Instance = instance,
        .PhysicalDevice = physicalDevice,
        .Device = device,
        .QueueFamily = graphicsQueueFamilyIndex,
        .Queue = graphicsQueue,
        .PipelineCache = nullptr,
        .DescriptorPoolSize = 2,
        .RenderPass = renderSurface.getRenderPass(),
        .Subpass = 0,
        .MinImageCount = 3,
        .ImageCount = 3,
        .MSAASamples = config.msaaSamples,
        .Allocator = nullptr,
        .CheckVkResultFn = check_vk_result,
    };
    ImGui_ImplVulkan_Init(&init_info);

    typedef std::chrono::steady_clock Clock;
    auto lastUpdateTime = Clock::now();
    bool running = true;
    SDL_Event event;
    while (running) {
        static const float maxFrameTime = 1.0f / 30.0f;
        auto now = Clock::now();
        float dt = std::chrono::duration<float>(now - lastUpdateTime).count();
        dt = glm::min(dt, maxFrameTime);
        lastUpdateTime = now;

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

        RenderSurface::Frame frame = renderSurface.beginFrame();

        pipeline.draw(
            frame.commandBuffer,
            camera.getProjectionMatrix(),
            camera.getViewMatrix(),
            meshObjects,
            lights
        );

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        bool configChanged = renderingConfigGui(stagingConfig, dt, physicalDeviceProperties);
        ImGui::Render();
        ImDrawData* imguiDrawData = ImGui::GetDrawData();
        ImGui_ImplVulkan_RenderDrawData(imguiDrawData, frame.commandBuffer);
        // std::cout << "draw imgui done" << std::endl;

        renderSurface.endFrame(frame);

        if (configChanged) {
            vkDeviceWaitIdle(device);
            RenderingConfig oldConfig = config;
            config = stagingConfig;
            if (config.maxAnisotropy != oldConfig.maxAnisotropy || config.useMipMaps != oldConfig.useMipMaps) {
                for (auto& obj : meshObjects) {
                    obj.textureSampler = createTextureSampler(device, config.maxAnisotropy, config.useMipMaps ? obj.mipLevels : 0);
                    obj.materialDescriptorSet = transferMaterialToGpu(obj.material, pipeline, obj.textureSampler, obj.textureImageView);
                }
            }
            if (config.vsyncEnabled != oldConfig.vsyncEnabled) {
                renderSurface.setVsync(config.vsyncEnabled);
            }
            if (config.msaaSamples != oldConfig.msaaSamples) {
                renderSurface.setMsaaSamples(config.msaaSamples);
                pipeline.setMsaaSamples(config.msaaSamples, renderSurface.getRenderPass());

                ImGui_ImplVulkan_Shutdown();
                ImGui_ImplVulkan_InitInfo init_info {
                    .Instance = instance,
                    .PhysicalDevice = physicalDevice,
                    .Device = device,
                    .QueueFamily = graphicsQueueFamilyIndex,
                    .Queue = graphicsQueue,
                    .PipelineCache = nullptr,
                    .DescriptorPoolSize = 2,
                    .RenderPass = renderSurface.getRenderPass(),
                    .Subpass = 0,
                    .MinImageCount = 2,
                    .ImageCount = 2,
                    .MSAASamples = config.msaaSamples,
                    .Allocator = nullptr,
                    .CheckVkResultFn = check_vk_result,
                };
                ImGui_ImplVulkan_Init(&init_info);
            }
        }
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
