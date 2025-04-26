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

#include <ktxvulkan.h>

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
#include "Profiler.h"
#include "TextureLoader.h"


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

class FrameLevelResources {
public:
    struct Light {
        glm::vec3 pos;
        float _padding1;
        glm::vec3 diffuseFactor;
        float _padding2;
    };

    FrameLevelResources(
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        uint32_t framesInFlight
    ):
        m_device(device),
        m_viewProjection(physicalDevice, device, framesInFlight),
        m_lightBlock(physicalDevice, device, framesInFlight)
    {
        m_descriptorPool = createDescriptorPool(device, framesInFlight);
        m_descriptorSetLayout = createDescriptorSetLayout(device);
        m_descriptorSets = createDescriptorSets(framesInFlight);
    }

    VkDescriptorSetLayout descriptorSetLayout() const {return m_descriptorSetLayout;}
    VkDescriptorSet descriptorSet(int frameIndex) const {return m_descriptorSets[frameIndex];}

    void setViewProjection(int frameIndex, glm::mat4 const& view, glm::mat4 const& projection) {
        m_viewProjection.data()[frameIndex] = {view, projection};
    }

    void setLights(int frameIndex, std::vector<Light> const& lights) {
        LightBlock& lightBlock = m_lightBlock.data()[frameIndex];
        lightBlock.lightCount = (int) std::min(lightBlock.lights.size(), lights.size());
        std::copy_n(std::begin(lights), lightBlock.lightCount, std::begin(lightBlock.lights));
    }

    void setEnvironment(int frameIndex, VkImageView imageView, VkSampler sampler) {
        VkDescriptorImageInfo envInfo {
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .imageView = imageView,
            .sampler = sampler,
        };
        std::array writes = {
            VkWriteDescriptorSet{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = m_descriptorSets[frameIndex],
                .dstBinding = 2,
                .dstArrayElement = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .pImageInfo = &envInfo,
            },
        };
        vkUpdateDescriptorSets(m_device, writes.size(), writes.data(), 0, nullptr);
    }

private:
    struct ViewProjection {
        glm::mat4 view;
        glm::mat4 projection;
    };

    struct LightBlock {
        std::array<Light, 8> lights;
        int lightCount;
        int _pad1;
        int _pad2;
        int _pad3;
    };

    static VkDescriptorSetLayout createDescriptorSetLayout(VkDevice device) {
        std::array bindings = {
            VkDescriptorSetLayoutBinding {
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            },
            VkDescriptorSetLayoutBinding {
                .binding = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
            VkDescriptorSetLayoutBinding {
                .binding = 2,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
        };
        VkDescriptorSetLayoutCreateInfo layoutInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = bindings.size(),
            .pBindings = bindings.data(),
        };
        
        VkDescriptorSetLayout descriptorSetLayout;
        vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout);
        return descriptorSetLayout;
    }

    static VkDescriptorPool createDescriptorPool(VkDevice device, uint32_t framesInFlight) {
        std::array poolSizes = {
            VkDescriptorPoolSize{.type=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount=2 * framesInFlight},
            VkDescriptorPoolSize{.type=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount=1 * framesInFlight},
        };
        VkDescriptorPoolCreateInfo poolInfo {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .poolSizeCount = poolSizes.size(),
            .pPoolSizes = poolSizes.data(),
            .maxSets = framesInFlight,
        };

        VkDescriptorPool descriptorPool;
        vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool);
        return descriptorPool;
    }

    std::vector<VkDescriptorSet> createDescriptorSets(uint32_t framesInFlight) {
        std::vector<VkDescriptorSetLayout> layouts(framesInFlight, m_descriptorSetLayout);
        VkDescriptorSetAllocateInfo allocInfo {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = m_descriptorPool,
            .descriptorSetCount = framesInFlight,
            .pSetLayouts = layouts.data(),
        };
        std::vector<VkDescriptorSet> descriptorSets(framesInFlight);
        vkAllocateDescriptorSets(m_device, &allocInfo, descriptorSets.data());
        for (uint32_t i = 0; i < framesInFlight; i++) {
            VkDescriptorBufferInfo viewProjectionBufferInfo = m_viewProjection.descriptorBufferInfo(i);
            VkDescriptorBufferInfo lightBlockBufferInfo = m_lightBlock.descriptorBufferInfo(i);
            std::array writes = {
                VkWriteDescriptorSet{
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = descriptorSets[i],
                    .dstBinding = 0,
                    .dstArrayElement = 0,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .descriptorCount = 1,
                    .pBufferInfo = &viewProjectionBufferInfo,
                },
                VkWriteDescriptorSet{
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = descriptorSets[i],
                    .dstBinding = 1,
                    .dstArrayElement = 0,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .descriptorCount = 1,
                    .pBufferInfo = &lightBlockBufferInfo,
                },
            };
            vkUpdateDescriptorSets(m_device, writes.size(), writes.data(), 0, nullptr);
        }
        return descriptorSets;
    }

    VkDevice m_device;
    VkDescriptorSetLayout m_descriptorSetLayout;
    VkDescriptorPool m_descriptorPool;
    std::vector<VkDescriptorSet> m_descriptorSets;
    UniformBuffer<ViewProjection[]> m_viewProjection;
    UniformBuffer<LightBlock[]> m_lightBlock;
};

int main() {
    PROFILE_ME;
    VulkanContext vulkanContext;
    RenderingConfig config {
        .vsyncEnabled = true,
        .maxAnisotropy = vulkanContext.physicalDeviceProperties.limits.maxSamplerAnisotropy,
        .msaaSamples = VK_SAMPLE_COUNT_4_BIT,
    };

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

    // TODO
    // Use ImGuiSelectableFlags_Disabled for surface format groups (surfaceFormatLabels) which don't have no surface format supported
    // When we select from surfaceFormatLabels - pass new preferredSurfaceFormats array to RenderSurface
    std::vector<VkSurfaceFormatKHR> preferredSurfaceFormats = {
        // Ideal: linear color, float format (HDR)
        { VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT },
        { VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_BT2020_LINEAR_EXT },
        { VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT },

        // Acceptable: 10-bit UNORM linear (less precision but linear)
        { VK_FORMAT_A2R10G10B10_UNORM_PACK32, VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT },
        { VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT },

        // Fallbacks: sRGB, with automatic gamma correction on write
        { VK_FORMAT_R8G8B8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR },
        { VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR },
    };
    RenderSurface renderSurface({
        .instance = vulkanContext.instance,
        .physicalDevice = vulkanContext.physicalDevice,
        .device = vulkanContext.device,
        .window = window,
        .preferredSurfaceFormats = preferredSurfaceFormats,
        .graphicsQueue = vulkanContext.graphicsQueue,
        .presentQueue = vulkanContext.graphicsQueue,
        .graphicsQueueFamilyIndex = vulkanContext.graphicsQueueFamilyIndex,
        .extent = {width, height},
        .framesInFlight = 3,
        .vsyncEnabled = config.vsyncEnabled,
        .msaaSamples = config.msaaSamples,
    });
    config.surfaceFormat = renderSurface.getFormat();

    TextureLoader textureLoader(
        vulkanContext.physicalDevice,
        vulkanContext.device,
        vulkanContext.graphicsQueue,
        vulkanContext.commandPool
    );
    VkImage environmentImage1;
    VkImageView environmentImageView1;
    loadCubemap(
        vulkanContext.physicalDevice,
        vulkanContext.device,
        vulkanContext.commandPool,
        vulkanContext.graphicsQueue,
        {
            "build/golden_gate_hills_4k/px.exr",
            "build/golden_gate_hills_4k/nx.exr",
            "build/golden_gate_hills_4k/py.exr",
            "build/golden_gate_hills_4k/ny.exr",
            "build/golden_gate_hills_4k/pz.exr",
            "build/golden_gate_hills_4k/nz.exr",
        },
        &environmentImage1,
        &environmentImageView1
    );
    VkImage environmentImage2;
    VkImageView environmentImageView2;
    loadCubemap(
        vulkanContext.physicalDevice,
        vulkanContext.device,
        vulkanContext.commandPool,
        vulkanContext.graphicsQueue,
        {
            "build/mirrored_hall_1k/px.exr",
            "build/mirrored_hall_1k/nx.exr",
            "build/mirrored_hall_1k/py.exr",
            "build/mirrored_hall_1k/ny.exr",
            "build/mirrored_hall_1k/pz.exr",
            "build/mirrored_hall_1k/nz.exr",
        },
        &environmentImage2,
        &environmentImageView2
    );
    VkImage environmentImage4;
    VkImageView environmentImageView4;
    loadCubemap(
        vulkanContext.physicalDevice,
        vulkanContext.device,
        vulkanContext.commandPool,
        vulkanContext.graphicsQueue,
        {
            "assets/debug-cubemap/px.png",
            "assets/debug-cubemap/nx.png",
            "assets/debug-cubemap/py.png",
            "assets/debug-cubemap/ny.png",
            "assets/debug-cubemap/pz.png",
            "assets/debug-cubemap/nz.png",
        },
        &environmentImage4,
        &environmentImageView4
    );

    VkImageView environmentImageView3 = textureLoader.loadKtx("assets/cubemap_yokohama_rgba.ktx");

    std::array environmentLabels = {
        "Golden Gate Hills",
        "Mirrored Hall",
        "Yokohama",
        "Debug Cubemap",
    };
    std::array environmentImageViews = {
        environmentImageView1,
        environmentImageView2,
        environmentImageView3,
        environmentImageView4,
    };

    std::vector<VkSurfaceFormatKHR> supportedSurfaceFormats;
    for (const auto& surfaceFormat : preferredSurfaceFormats) {
        if (renderSurface.isFormatSupported(surfaceFormat)) {
            supportedSurfaceFormats.push_back(surfaceFormat);
        }
    }

    RenderingConfigOptions renderingConfigOptions {
        .physicalDeviceProperties = vulkanContext.physicalDeviceProperties,
        .environments = environmentLabels,
        .surfaceFormats = supportedSurfaceFormats,
    };

    VkImageView environmentImageView = environmentImageViews[0];
    VkSampler environmentSampler = createTextureSampler(vulkanContext.device, config.maxAnisotropy, 0);

    FrameLevelResources frameLevelResources(
        vulkanContext.physicalDevice,
        vulkanContext.device,
        renderSurface.getFramesInFlight()
    );

    CubemapBackgroundPipeline backgroundPipeline(
        vulkanContext.device,
        renderSurface.getExtent(),
        renderSurface.getRenderPass(),
        renderSurface.getMsaaSamples(),
        frameLevelResources.descriptorSetLayout()
    );

    Pipeline pipeline(
        vulkanContext.physicalDevice,
        vulkanContext.device,
        renderSurface.getExtent(),
        renderSurface.getRenderPass(),
        renderSurface.getMsaaSamples(),
        frameLevelResources.descriptorSetLayout(),
        1024
    );
    std::vector<MeshObject> meshObjects;
    std::vector<FrameLevelResources::Light> lights;

    {
        Model woodenStoolModel = loadObj("assets/wooden_stool_02_4k.obj");
        woodenStoolModel.material.specularHardness = 500;
        woodenStoolModel.material.specularPower = 5;
        MeshObject woodenStool = transferModelToGpu(vulkanContext, config.maxAnisotropy, pipeline, woodenStoolModel);
        meshObjects.push_back(woodenStool);
    }

    {
        Model lightModel1;
        lightModel1.vertices = createSphereMesh(2, 0.05);
        lightModel1.material.diffuseFactor = glm::vec3{0.0f};
        lightModel1.material.emitFactor = 5.0f * glm::vec3{1.0f, 0.5f, 0.5f};
        MeshObject lightObj1 = transferModelToGpu(vulkanContext, config.maxAnisotropy, pipeline, lightModel1);
        lightObj1.position = {-1.5f, 1.0f, 1.0f};
        meshObjects.push_back(lightObj1);
        lights.push_back(FrameLevelResources::Light{.pos=lightObj1.position, .diffuseFactor={1.0f, 0.5f, 0.4f}});
    }

    {
        Model lightModel2;
        lightModel2.vertices = createSphereMesh(2, 0.05);
        lightModel2.material.diffuseFactor = glm::vec3{0.0f};
        lightModel2.material.emitFactor = 5.0f * glm::vec3{0.5f, 0.5f, 1.0f};
        MeshObject lightObj2 = transferModelToGpu(vulkanContext, config.maxAnisotropy, pipeline, lightModel2);
        lightObj2.position = {1.5f, 1.0f, 1.0f};
        meshObjects.push_back(lightObj2);
        lights.push_back(FrameLevelResources::Light{.pos=lightObj2.position, .diffuseFactor={0.3f, 0.5f, 1.0f}});
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
        .DescriptorPoolSize = 2,
        .RenderPass = renderSurface.getRenderPass(),
        .Subpass = 0,
        .MinImageCount = 3,
        .ImageCount = 3,
        .MSAASamples = config.msaaSamples,
    };
    ImGui_ImplVulkan_Init(&init_info);

    PROFILE_END;
    profiler::getInstance().print(std::cout, 60);

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

        frameLevelResources.setViewProjection(frame.swapchainImageIndex, camera.getViewMatrix(), camera.getProjectionMatrix());
        // frameLevelResources.setLights(frame.swapchainImageIndex, lights);
        frameLevelResources.setEnvironment(frame.swapchainImageIndex, environmentImageView, environmentSampler);

        backgroundPipeline.draw(
            frame.commandBuffer,
            frameLevelResources.descriptorSet(frame.swapchainImageIndex)
        );

        pipeline.draw(
            frame.commandBuffer,
            frameLevelResources.descriptorSet(frame.swapchainImageIndex),
            meshObjects
        );

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        RenderingConfig stagingConfig = config;
        bool configChanged = renderingConfigGui(stagingConfig, renderingConfigOptions, dt);
        ImGui::Render();
        ImDrawData* imguiDrawData = ImGui::GetDrawData();
        ImGui_ImplVulkan_RenderDrawData(imguiDrawData, frame.commandBuffer);

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
            }
            if (config.environmentIndex != oldConfig.environmentIndex) {
                environmentImageView = environmentImageViews[config.environmentIndex];
            }
            if (config.surfaceFormat != oldConfig.surfaceFormat) {
                renderSurface.setFormat(config.surfaceFormat);
            }

            pipeline.updateRenderPass(renderSurface.getRenderPass(), config.msaaSamples);
            backgroundPipeline.updateRenderPass(renderSurface.getRenderPass(), config.msaaSamples);

            ImGui_ImplVulkan_Shutdown();
            ImGui_ImplVulkan_InitInfo init_info {
                .Instance = vulkanContext.instance,
                .PhysicalDevice = vulkanContext.physicalDevice,
                .Device = vulkanContext.device,
                .QueueFamily = vulkanContext.graphicsQueueFamilyIndex,
                .Queue = vulkanContext.graphicsQueue,
                .DescriptorPoolSize = 2,
                .RenderPass = renderSurface.getRenderPass(),
                .Subpass = 0,
                .MinImageCount = 2,
                .ImageCount = 2,
                .MSAASamples = config.msaaSamples,
            };
            ImGui_ImplVulkan_Init(&init_info);
        }
    }

    // Cleanup
    vkDeviceWaitIdle(vulkanContext.device);
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    // First need to delete all child objects
    // vkDestroyInstance(instance, nullptr);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
