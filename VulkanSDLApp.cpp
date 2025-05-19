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
#include "ColorTemperature.h"
#include "CubemapFunctions.h"
#include "Environment.h"
#include "FileFunctions.h"


VkDescriptorSet transferMaterialToGpu(
    Material const& material,
    Pipeline& pipeline,
    VkImageView baseColorImageView,
    VkSampler baseColorSampler,
    VkImageView roughnessImageView,
    VkSampler roughnessSampler
) {
    Pipeline::MaterialProps materialProps{
        .baseColorFactor = material.baseColorFactor,
        .emitFactor = material.emitFactor,
        .roughnessFactor = material.roughnessFactor,
        .metallicFactor = material.metallicFactor,
    };
    return pipeline.createMaterial(baseColorImageView, baseColorSampler, roughnessImageView, roughnessSampler, materialProps);
}

ImageData loadTextureOrDefault(std::string const& fileName, glm::vec4 defaultValue) {
    ImageData imageData;
    if (fileName.empty()) {
        uint8_t * whitePixelData = (uint8_t*) malloc(4);
        whitePixelData[0] = 255 * defaultValue[0];
        whitePixelData[1] = 255 * defaultValue[1];
        whitePixelData[2] = 255 * defaultValue[2];
        whitePixelData[3] = 255 * defaultValue[3];
        imageData.data.reset((void*) whitePixelData);
        imageData.imageFormat = VK_FORMAT_R8G8B8A8_UNORM;
        imageData.dataSize = 4;
        imageData.width = 1;
        imageData.height = 1;
    }
    else {
        imageData = loadImage(fileName);
    }
    return imageData;
}

MeshObject transferModelToGpu(VulkanContext vulkanContext, float maxAnisotropy, Pipeline& pipeline, const Model& model) {
    MeshObject object{};
    object.vertexBuffer = createVertexBuffer(vulkanContext.physicalDevice, vulkanContext.device, model.vertices);
    object.vertexCount = model.vertices.size();

    ImageData baseColorImageData = loadTextureOrDefault(model.material.baseColorTexture, glm::vec4 {1.0f});
    object.baseColorMipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(baseColorImageData.width, baseColorImageData.height)))) + 1;
    object.baseColorImage = createTextureImage(vulkanContext.physicalDevice, vulkanContext.device, vulkanContext.commandPool, vulkanContext.graphicsQueue, baseColorImageData, object.baseColorMipLevels);
    object.baseColorImageView = createImageView(vulkanContext.device, object.baseColorImage, baseColorImageData.imageFormat, object.baseColorMipLevels);
    object.baseColorSampler = createTextureSampler(vulkanContext.device, maxAnisotropy, object.baseColorMipLevels);

    ImageData roughnessImageData = loadTextureOrDefault(model.material.roughnessTexture, glm::vec4 {1.0f});
    object.roughnessMipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(roughnessImageData.width, roughnessImageData.height)))) + 1;
    object.roughnessImage = createTextureImage(vulkanContext.physicalDevice, vulkanContext.device, vulkanContext.commandPool, vulkanContext.graphicsQueue, roughnessImageData, object.roughnessMipLevels);
    object.roughnessImageView = createImageView(vulkanContext.device, object.roughnessImage, roughnessImageData.imageFormat, object.roughnessMipLevels);
    object.roughnessSampler = createTextureSampler(vulkanContext.device, maxAnisotropy, object.roughnessMipLevels);
    
    object.material = model.material;
    object.materialDescriptorSet = transferMaterialToGpu(
        model.material,
        pipeline,
        object.baseColorImageView,
        object.baseColorSampler,
        object.roughnessImageView,
        object.roughnessSampler
    );
    return object;
}

Environment::Sun loadSunFromYaml(const char* yamlFileName) {
    auto sunYaml = loadYaml(yamlFileName);
    glm::vec3 dir {
        sunYaml["dir"][0].as_float(),
        sunYaml["dir"][1].as_float(),
        sunYaml["dir"][2].as_float(),
    };
    glm::vec3 radiance {
        sunYaml["radiance"][0].as_float(),
        sunYaml["radiance"][1].as_float(),
        sunYaml["radiance"][2].as_float(),
    };
    float solidAngle = sunYaml["solidAngle"].as_float();
    return {dir, radiance, solidAngle};
}

class FrameLevelResources {
public:
    struct Light {
        glm::vec3 pos;
        float _padding1;
        glm::vec3 diffuseFactor;
        float _padding2;
    };

    struct SunUBO {
        glm::vec3 dir;
        float _padding1;
        glm::vec3 radiance;
        float _padding2;
        float solidAngle;
        glm::vec3 _padding3;
    };

    struct SphericalHarmonics {
        std::array<glm::vec4, 9> lambertianSphericalHamonics;
    };

    FrameLevelResources(
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        uint32_t framesInFlight,
        VkImageView brdfLut,
        VkSampler brdfLutSampler
    ):
        m_device(device),
        m_viewProjection(physicalDevice, device, framesInFlight),
        m_lightBlock(physicalDevice, device, framesInFlight),
        m_diffuseSphericalHarmonics(physicalDevice, device, framesInFlight),
        m_sunBuffer(physicalDevice, device, framesInFlight)
    {
        m_descriptorPool = createDescriptorPool(device, framesInFlight);
        m_descriptorSetLayout = createDescriptorSetLayout(device);
        m_descriptorSets = createDescriptorSets(framesInFlight, brdfLut, brdfLutSampler);
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

    void setEnvironment(int frameIndex, Environment const& env, VkSampler sampler) {
        VkDescriptorImageInfo envInfo {
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .imageView = env.backgroundImageView,
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

        m_diffuseSphericalHarmonics.data()[frameIndex] = {};
        for (size_t i = 0; i < env.diffuseSphericalHarmonics.size(); i++) {
            auto const& coeffs = env.diffuseSphericalHarmonics[i];
            glm::vec4& dst = m_diffuseSphericalHarmonics.data()[frameIndex].lambertianSphericalHamonics[i];
            dst[0] = coeffs[0];
            dst[1] = coeffs[1];
            dst[2] = coeffs[2];
        }

        m_sunBuffer.data()[frameIndex] = {
            .dir=env.sun.dir,
            .radiance=env.sun.radiance,
            .solidAngle=env.sun.solidAngle,
        };
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
            VkDescriptorSetLayoutBinding {
                .binding = 3,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
            VkDescriptorSetLayoutBinding {
                .binding = 4,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
            VkDescriptorSetLayoutBinding {
                .binding = 5,
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
            VkDescriptorPoolSize{.type=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount=4 * framesInFlight},
            VkDescriptorPoolSize{.type=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount=2 * framesInFlight},
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

    std::vector<VkDescriptorSet> createDescriptorSets(
        uint32_t framesInFlight,
        VkImageView brdfLut,
        VkSampler brdfLutSampler
    ) {
        VkDescriptorImageInfo brdfLutInfo {
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .imageView = brdfLut,
            .sampler = brdfLutSampler,
        };

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
            VkDescriptorBufferInfo diffuseHarmonicsBufferInfo = m_diffuseSphericalHarmonics.descriptorBufferInfo(i);
            VkDescriptorBufferInfo sunBufferInfo = m_sunBuffer.descriptorBufferInfo(i);
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
                VkWriteDescriptorSet{
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = descriptorSets[i],
                    .dstBinding = 3,
                    .dstArrayElement = 0,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .descriptorCount = 1,
                    .pBufferInfo = &diffuseHarmonicsBufferInfo,
                },
                VkWriteDescriptorSet{
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = descriptorSets[i],
                    .dstBinding = 4,
                    .dstArrayElement = 0,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .descriptorCount = 1,
                    .pBufferInfo = &sunBufferInfo,
                },
                VkWriteDescriptorSet{
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = descriptorSets[i],
                    .dstBinding = 5,
                    .dstArrayElement = 0,
                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .descriptorCount = 1,
                    .pImageInfo = &brdfLutInfo,
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
    UniformBuffer<SphericalHarmonics[]> m_diffuseSphericalHarmonics;
    UniformBuffer<SunUBO[]> m_sunBuffer;
};

void sphericalHarmonicsGui(std::vector<glm::vec3>& shCoeffs) {
    ImGui::Begin("Spherical Harmonics");
    for (size_t i = 0; i < shCoeffs.size(); i++) {
        glm::vec3& vec = shCoeffs[i];
        ImGui::DragFloat3(std::to_string(i).c_str(), &vec[0], 0.01f);
    }
    ImGui::End();
}

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

    TextureLoader textureLoader(
        vulkanContext.physicalDevice,
        vulkanContext.device,
        vulkanContext.graphicsQueue,
        vulkanContext.commandPool
    );

    VkImageView brdfLut = textureLoader.loadKtx("build/brdf.ktx2");
    VkSampler brdfLutSampler = createLookupTableSampler(vulkanContext.device);

    uint32_t framesInFlight = 3;
    FrameLevelResources frameLevelResources(
        vulkanContext.physicalDevice,
        vulkanContext.device,
        framesInFlight,
        brdfLut,
        brdfLutSampler
    );

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
        .framesInFlight = framesInFlight,
        .vsyncEnabled = config.vsyncEnabled,
        .msaaSamples = config.msaaSamples,
        .frameLevelDescriptorSetLayout = frameLevelResources.descriptorSetLayout(),
        .renderInFormat = VK_FORMAT_R16G16B16A16_SFLOAT,
    });
    config.surfaceFormat = renderSurface.getFormat();

    std::vector<Environment> environments = {
        {
            textureLoader.loadKtx("build/golden_gate_hills_4k.ktx2"),
            loadSHCoeffs("build/golden_gate_hills_4k.sh.txt"),
            loadSunFromYaml("build/golden_gate_hills_4k.sun.yaml"),
        },
        {
            textureLoader.loadKtx("build/mirrored_hall_1k.ktx2"),
            loadSHCoeffs("build/mirrored_hall_1k.sh.txt"),
        },
        {
            textureLoader.loadKtx("assets/cubemap_yokohama_rgba.ktx"),
            {{0.5f, 0.5f, 1.0f}},
        },
        {
            textureLoader.loadCubemap(
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
                }
            ),
            .sun={
                .dir={-0.432382f, -0.678913f, 0.593399f},
                .radiance={96891.0f, 98097.0f, 100099.0f},
            },
        },
    };
    std::array environmentLabels = {
        "Golden Gate Hills",
        "Mirrored Hall",
        "Yokohama",
        "Debug Cubemap",
    };
    VkSampler environmentSampler = createEnvironmentSampler(vulkanContext.device, config.maxAnisotropy);

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
        MeshObject woodenStool = transferModelToGpu(vulkanContext, config.maxAnisotropy, pipeline, woodenStoolModel);
        meshObjects.push_back(woodenStool);
    }

    {
        glm::vec3 color = temperatureToRgb(1000);
        float intensity = 0.5f;
        Model lightModel1;
        lightModel1.vertices = createSphereMesh(2, 0.03);
        lightModel1.material.baseColorFactor = glm::vec3{0.0f};
        lightModel1.material.emitFactor = 10.0f * color;
        MeshObject lightObj1 = transferModelToGpu(vulkanContext, config.maxAnisotropy, pipeline, lightModel1);
        lightObj1.position = {-1.5f, 1.5f, 0.0f};
        meshObjects.push_back(lightObj1);
        lights.push_back(FrameLevelResources::Light{.pos=lightObj1.position, .diffuseFactor=intensity * color});
        lights.pop_back();
    }

    {
        glm::vec3 color = temperatureToRgb(25000);
        float intensity = 1.5f;
        Model lightModel2;
        lightModel2.vertices = createSphereMesh(2, 0.05);
        lightModel2.material.baseColorFactor = glm::vec3{0.0f};
        lightModel2.material.emitFactor = 10.0f * color;
        MeshObject lightObj2 = transferModelToGpu(vulkanContext, config.maxAnisotropy, pipeline, lightModel2);
        lightObj2.position = {1.5f, 1.5f, 0.0f};
        meshObjects.push_back(lightObj2);
        lights.push_back(FrameLevelResources::Light{.pos=lightObj2.position, .diffuseFactor=intensity * color});
        lights.pop_back();
    }

    {
        std::vector<Vertex> vertices;
        vertices.push_back({{-1.0f, 0, 1.0f}, {0, 1.0f, 0}, {1.0f, 1.0f, 1.0f}, {0, 0}});
        vertices.push_back({{1.0f, 0, 1.0f}, {0, 1.0f, 0}, {1.0f, 1.0f, 1.0f}, {1, 0}});
        vertices.push_back({{1.0f, 0, -1.0f}, {0, 1.0f, 0}, {1.0f, 1.0f, 1.0f}, {1, 1}});

        vertices.push_back({{1.0f, 0, -1.0f}, {0, 1.0f, 0}, {1.0f, 1.0f, 1.0f}, {1, 1}});
        vertices.push_back({{-1.0f, 0, -1.0f}, {0, 1.0f, 0}, {1.0f, 1.0f, 1.0f}, {0, 1}});
        vertices.push_back({{-1.0f, 0, 1.0f}, {0, 1.0f, 0}, {1.0f, 1.0f, 1.0f}, {0, 0}});

        Material floorMaterial{
            .baseColorFactor = {0.7f, 0.7f, 0.7f},
            .roughnessFactor=0.35,
        };
        Model model{vertices, floorMaterial};
        MeshObject floorObj = transferModelToGpu(vulkanContext, config.maxAnisotropy, pipeline, model);
        meshObjects.push_back(floorObj);
    }

    for (int x = 0; x < 4; x++) {
        for (int y = 0; y < 4; y++) {
            static std::array roughness = {0.1f, 0.4f, 0.5f, 1.0f};
            static std::array metallic = {0.0f, 0.35f, 0.65f, 1.0f};
            Material material {
                .baseColorFactor = glm::vec3(0.7f),
                .roughnessFactor = roughness[x],
                .metallicFactor = metallic[y],
            };
            Model model {createSphereMesh(4, 0.2), material};
            MeshObject meshObj = transferModelToGpu(vulkanContext, config.maxAnisotropy, pipeline, model);
            meshObj.position = 0.5f * (glm::vec3{x - 1.5f, y, 0}) + glm::vec3{0, 0, -2.0f};
            meshObjects.push_back(meshObj);
        }
    }

    Camera camera;
    camera.setFOV(45.0f);
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
        .Subpass = 1,
        .MinImageCount = 3,
        .ImageCount = 3,
        .MSAASamples = VK_SAMPLE_COUNT_1_BIT,
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
        frameLevelResources.setLights(frame.swapchainImageIndex, lights);
        frameLevelResources.setEnvironment(frame.swapchainImageIndex, environments[config.environmentIndex], environmentSampler);

        backgroundPipeline.draw(
            frame.commandBuffer,
            frameLevelResources.descriptorSet(frame.swapchainImageIndex)
        );

        pipeline.draw(
            frame.commandBuffer,
            frameLevelResources.descriptorSet(frame.swapchainImageIndex),
            meshObjects
        );

        renderSurface.setTonemappingParameters(config.tonemapOperator, config.exposure, config.reinhardWhitePoint);
        renderSurface.postprocess(frame, frameLevelResources.descriptorSet(frame.swapchainImageIndex));

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        RenderingConfig stagingConfig = config;
        bool configChanged = renderingConfigGui(stagingConfig, renderingConfigOptions, dt);
        sphericalHarmonicsGui(environments[config.environmentIndex].diffuseSphericalHarmonics);
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
                    obj.baseColorSampler = createTextureSampler(vulkanContext.device, config.maxAnisotropy, config.useMipMaps ? obj.baseColorMipLevels : 0);
                    obj.roughnessSampler = createTextureSampler(vulkanContext.device, config.maxAnisotropy, config.useMipMaps ? obj.roughnessMipLevels : 0);
                    obj.materialDescriptorSet = transferMaterialToGpu(
                        obj.material,
                        pipeline,
                        obj.baseColorImageView,
                        obj.baseColorSampler,
                        obj.roughnessImageView,
                        obj.roughnessSampler
                    );
                }
            }
            if (config.vsyncEnabled != oldConfig.vsyncEnabled) {
                renderSurface.setVsync(config.vsyncEnabled);
            }
            if (config.msaaSamples != oldConfig.msaaSamples) {
                renderSurface.setMsaaSamples(config.msaaSamples);
            }
            if (config.surfaceFormat != oldConfig.surfaceFormat) {
                renderSurface.setDisplayFormat(config.surfaceFormat);
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
                .Subpass = 1,
                .MinImageCount = 3,
                .ImageCount = 3,
                .MSAASamples = VK_SAMPLE_COUNT_1_BIT,
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
