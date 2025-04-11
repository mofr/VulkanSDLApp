#include <iostream>
#include <fstream>
#include <map>
#include <chrono>
#include <SDL.h>
#include <SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <glm/glm.hpp>
#include <glm/ext.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_vulkan.h>

#include "Pipeline.h"
#include "CubemapBackgroundPipeline.h"
#include "Vertex.h"
#include "VulkanContext.h"
#include "VulkanFunctions.h"
#include "Model.h"
#include "Camera.h"
#include "ObjFile.h"
#include "MeshObject.h"
#include "MeshFunctions.h"
#include "OrbitCameraController.h"
#include "FlyingCameraController.h"
#include "RenderSurface.h"
#include "RenderingConfig.h"


VkDescriptorSet transferMaterialToGpu(Material const& material, Pipeline& pipeline, VkSampler sampler, VkImageView textureImageView) {
    Pipeline::MaterialProps materialProps{
        .diffuseFactor = material.diffuseFactor,
        .emitFactor = material.emitFactor,
        .specularHardness = material.specularHardness,
        .specularPower = material.specularPower,
    };
    return pipeline.createMaterial(textureImageView, sampler, materialProps);
}

MeshObject transferModelToGpu(VulkanContext vulkanContext, float maxAnisotropy, Pipeline& pipeline, const Model& model) {
    MeshObject object{};
    object.vertexBuffer = createVertexBuffer(vulkanContext.physicalDevice, vulkanContext.device, model.vertices);
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
    object.textureImage = createTextureImage(vulkanContext.physicalDevice, vulkanContext.device, vulkanContext.commandPool, vulkanContext.graphicsQueue, imageData, object.mipLevels);
    object.textureImageView = createImageView(vulkanContext.device, object.textureImage, VK_FORMAT_R8G8B8A8_SRGB, object.mipLevels);
    object.material = model.material;
    object.textureSampler = createTextureSampler(vulkanContext.device, maxAnisotropy, object.mipLevels);
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

int main() {
    VulkanContext vulkanContext;
    RenderingConfig config{
        .vsyncEnabled = true,
        .maxAnisotropy = vulkanContext.physicalDeviceProperties.limits.maxSamplerAnisotropy,
        .msaaSamples = VK_SAMPLE_COUNT_4_BIT,
    };
    RenderingConfig stagingConfig = config;

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
    RenderSurface renderSurface({
        .instance = vulkanContext.instance,
        .physicalDevice = vulkanContext.physicalDevice,
        .device = vulkanContext.device,
        .window = window,
        .graphicsQueue = vulkanContext.graphicsQueue,
        .presentQueue = vulkanContext.graphicsQueue,
        .graphicsQueueFamilyIndex = vulkanContext.graphicsQueueFamilyIndex,
        .extent = {width, height},
        .framesInFlight = 3,
        .vsyncEnabled = config.vsyncEnabled,
        .msaaSamples = config.msaaSamples,
    });

    VkImage environmentImage;
    VkImageView environmentImageView;
    loadCubemap(
        vulkanContext.physicalDevice,
        vulkanContext.device,
        vulkanContext.commandPool,
        vulkanContext.graphicsQueue,
        {
            "resources/rosendal_plains_1_8k/px.exr",
            "resources/rosendal_plains_1_8k/nx.exr",
            "resources/rosendal_plains_1_8k/py.exr",
            "resources/rosendal_plains_1_8k/ny.exr",
            "resources/rosendal_plains_1_8k/pz.exr",
            "resources/rosendal_plains_1_8k/nz.exr",
        },
        &environmentImage,
        &environmentImageView
    );

    CubemapBackgroundPipeline backgroundPipeline(
        vulkanContext.physicalDevice,
        vulkanContext.device,
        renderSurface.getExtent(),
        renderSurface.getRenderPass(),
        renderSurface.getMsaaSamples(),
        environmentImageView
    );

    Pipeline pipeline(
        vulkanContext.physicalDevice,
        vulkanContext.device,
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
        MeshObject woodenStool = transferModelToGpu(vulkanContext, config.maxAnisotropy, pipeline, woodenStoolModel);
        meshObjects.push_back(woodenStool);
    }

    {
        Model lightModel1;
        lightModel1.vertices = createSphereMesh(2, 0.05);
        lightModel1.material.diffuseFactor = glm::vec3{0.0f};
        lightModel1.material.emitFactor = glm::vec3{1.0f, 0.5f, 0.5f};
        MeshObject lightObj1 = transferModelToGpu(vulkanContext, config.maxAnisotropy, pipeline, lightModel1);
        lightObj1.position = {-1.5f, 1.0f, 1.0f};
        meshObjects.push_back(lightObj1);
        lights.push_back(Pipeline::Light{.pos=lightObj1.position, .diffuseFactor={1.0f, 0.5f, 0.4f}});
    }

    {
        Model lightModel2;
        lightModel2.vertices = createSphereMesh(2, 0.05);
        lightModel2.material.diffuseFactor = glm::vec3{0.0f};
        lightModel2.material.emitFactor = glm::vec3{0.5f, 0.5f, 1.0f};
        MeshObject lightObj2 = transferModelToGpu(vulkanContext, config.maxAnisotropy, pipeline, lightModel2);
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
        floorObj = transferModelToGpu(vulkanContext, config.maxAnisotropy, pipeline, model);
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
        .Instance = vulkanContext.instance,
        .PhysicalDevice = vulkanContext.physicalDevice,
        .Device = vulkanContext.device,
        .QueueFamily = vulkanContext.graphicsQueueFamilyIndex,
        .Queue = vulkanContext.graphicsQueue,
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

        backgroundPipeline.draw(
            frame.commandBuffer,
            camera.getProjectionMatrix(),
            camera.getViewMatrix()
        );

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
        bool configChanged = renderingConfigGui(stagingConfig, dt, vulkanContext.physicalDeviceProperties);
        ImGui::Render();
        ImDrawData* imguiDrawData = ImGui::GetDrawData();
        ImGui_ImplVulkan_RenderDrawData(imguiDrawData, frame.commandBuffer);
        // std::cout << "draw imgui done" << std::endl;

        renderSurface.endFrame(frame);

        if (configChanged) {
            vkDeviceWaitIdle(vulkanContext.device);
            RenderingConfig oldConfig = config;
            config = stagingConfig;
            if (config.maxAnisotropy != oldConfig.maxAnisotropy || config.useMipMaps != oldConfig.useMipMaps) {
                for (auto& obj : meshObjects) {
                    obj.textureSampler = createTextureSampler(vulkanContext.device, config.maxAnisotropy, config.useMipMaps ? obj.mipLevels : 0);
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
                    .Instance = vulkanContext.instance,
                    .PhysicalDevice = vulkanContext.physicalDevice,
                    .Device = vulkanContext.device,
                    .QueueFamily = vulkanContext.graphicsQueueFamilyIndex,
                    .Queue = vulkanContext.graphicsQueue,
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
        VkResult err = vkDeviceWaitIdle(vulkanContext.device);
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
