#pragma once

#include <array>
#include <vulkan/vulkan.h>

#include "Vertex.h"
#include "VulkanFunctions.h"
#include "MeshObject.h"

/**
The class represents a concrete Vulkan pipeline to render textured meshes.
It requires a render pass with two attachments: color, depth.
It requires specific vertex format: Vertex.
Descriptor set layouts:
 Set 0:
  Binding 0: UBO with Model matrix
 Set 1:
  Binding 0: diffuse texture + sampler
  Binding 1: UBO material props
 Set 2:
  Binding 0: UBO with View and Projection matrices
  Binding 1: UBO lights
*/
class Pipeline {
    struct ModelTransform {
        glm::mat4 model;
    };

    struct ViewProjection {
        glm::mat4 view;
        glm::mat4 projection;
    };

    VkPhysicalDevice physicalDevice;
    VkDevice device;

    VkDescriptorPool descriptorPool;
    VkDescriptorSetLayout descriptorSetLayoutModelTransform;
    VkDescriptorSetLayout descriptorSetLayoutMaterial;
    VkDescriptorSetLayout descriptorSetLayoutViewProjection;
    VkDescriptorSet descriptorSetViewProjection;

    VkDeviceMemory modelTransformBufferMemory;
    VkBuffer modelTransformBuffer;
    std::vector<ModelTransform> modelTransforms;
    std::vector<VkDescriptorSet> modelTransformDescriptorSets;

    VkDeviceMemory viewProjectionBufferMemory;
    VkBuffer viewProjectionBuffer;

    VkDeviceMemory lightBlockBufferMemory;
    VkBuffer lightBlockBuffer;

public:
    VkPipelineLayout layout;
    VkPipeline pipeline;

    explicit Pipeline(VkPhysicalDevice physicalDevice, VkDevice device, VkExtent2D extent, VkRenderPass renderPass, uint32_t poolSize) {
        this->physicalDevice = physicalDevice;
        this->device = device;
        descriptorSetLayoutModelTransform = createDescriptorSetLayoutModelTransform(device);
        descriptorSetLayoutMaterial = createDescriptorSetLayoutMaterial(device);
        descriptorSetLayoutViewProjection = createDescriptorSetLayoutViewProjection(device);
        layout = createPipelineLayout(device, {descriptorSetLayoutModelTransform, descriptorSetLayoutMaterial, descriptorSetLayoutViewProjection});
        pipeline = createPipeline(device, extent, renderPass, layout);

        descriptorPool = createDescriptorPool(device, poolSize);
        createUniformBuffer(physicalDevice, device, viewProjectionBuffer, viewProjectionBufferMemory, sizeof(ViewProjection));
        createUniformBuffer(physicalDevice, device, modelTransformBuffer, modelTransformBufferMemory, poolSize * sizeof(ModelTransform));
        createUniformBuffer(physicalDevice, device, lightBlockBuffer, lightBlockBufferMemory, sizeof(LightBlock));
        descriptorSetViewProjection = createDescriptorSetViewProjection();
        modelTransformDescriptorSets = createDescriptorSetsModelTransforms(poolSize);
    }

    struct MaterialProps {
        glm::vec3 diffuseFactor{1.0f};
        float _padding1;
        glm::vec3 emitFactor{0.0f};
        float _padding2;
        float specularHardness = 1.0f;
        float specularPower = 1.0f;
    };

    struct Light {
        glm::vec3 pos;
        float _padding1;
        glm::vec3 diffuseFactor;
        float _padding2;
    };

    struct LightBlock {
        Light lights[8];
        int lightCount;
    };

    void draw(
        VkCommandBuffer commandBuffer,
        glm::mat4 const& projection,
        glm::mat4 const& view,
        std::vector<MeshObject> const& objects,
        std::vector<Light> const& lights
    ) {
        // TODO sort by Z to reduce overdraw?
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        {
            ViewProjection vp{};
            vp.view = view;
            vp.projection = projection;
            // TODO use persistently mapped buffers instead
            void* data;
            vkMapMemory(device, viewProjectionBufferMemory, 0, sizeof(vp), 0, &data);
            memcpy(data, &vp, sizeof(vp));
            vkUnmapMemory(device, viewProjectionBufferMemory);
        }
        {
            LightBlock lightBlock{};
            lightBlock.lightCount = std::min(8, (int)lights.size());
            for (int i = 0; i < lightBlock.lightCount; ++i) {
                lightBlock.lights[i] = lights[i];
            }
            void* data;
            vkMapMemory(device, lightBlockBufferMemory, 0, sizeof(lightBlock), 0, &data);
            memcpy(data, &lightBlock, sizeof(lightBlock));
            vkUnmapMemory(device, lightBlockBufferMemory);
        }
        {
            modelTransforms.reserve(objects.size());
            modelTransforms.clear();
            for (auto const& object : objects) {modelTransforms.push_back({object.getTransform()});}
            void* data;
            vkMapMemory(device, modelTransformBufferMemory, 0, modelTransforms.size() * sizeof(ModelTransform), 0, &data);
            memcpy(data, modelTransforms.data(), modelTransforms.size() * sizeof(ModelTransform));
            vkUnmapMemory(device, modelTransformBufferMemory);
        }

        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 2, 1, &descriptorSetViewProjection, 0, nullptr);

        // TODO group by vertex buffer and material
        for (uint32_t i = 0; i < objects.size(); i++) {
            auto const& object = objects[i];
            VkDescriptorSet transformDescriptorSet = modelTransformDescriptorSets[i];
            std::array descriptorSets = {transformDescriptorSet, object.material};
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, descriptorSets.size(), descriptorSets.data(), 0, nullptr);
            VkBuffer vertexBuffers[] = { object.vertexBuffer };
            VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
            vkCmdDraw(commandBuffer, object.vertexCount, 1, 0, 0);
        }
    }

    VkDescriptorSet createMaterial(VkImageView textureImageView, VkSampler textureSampler, MaterialProps const& props) {
        VkDeviceMemory materialPropsBufferMemory;
        VkBuffer materialPropsBuffer;
        createUniformBuffer(physicalDevice, device, materialPropsBuffer, materialPropsBufferMemory, sizeof(MaterialProps));
        {
            void* data;
            vkMapMemory(device, materialPropsBufferMemory, 0, sizeof(props), 0, &data);
            memcpy(data, &props, sizeof(props));
            vkUnmapMemory(device, materialPropsBufferMemory);
        }
        VkDescriptorSet materialDescriptorSet = createMaterialDescriptorSet();
        {
            VkDescriptorImageInfo imageInfo{};
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo.imageView = textureImageView;
            imageInfo.sampler = textureSampler;
            VkDescriptorBufferInfo materialPropsBufferInfo{
                .buffer=materialPropsBuffer,
                .range=sizeof(MaterialProps),
            };
            std::array writes = {
                VkWriteDescriptorSet{
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = materialDescriptorSet,
                    .dstBinding = 0,
                    .dstArrayElement = 0,
                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .descriptorCount = 1,
                    .pImageInfo = &imageInfo,
                },
                VkWriteDescriptorSet{
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = materialDescriptorSet,
                    .dstBinding = 1,
                    .dstArrayElement = 0,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .descriptorCount = 1,
                    .pBufferInfo = &materialPropsBufferInfo,
                },
            };
            vkUpdateDescriptorSets(device, writes.size(), writes.data(), 0, nullptr);
        }
        return materialDescriptorSet;
    }

    ~Pipeline() {

    }

private:
    VkDescriptorSet createMaterialDescriptorSet() const {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayoutMaterial;
        VkDescriptorSet descriptorSet;
        vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet);
        return descriptorSet;
    }

    std::vector<VkDescriptorSet> createDescriptorSetsModelTransforms(uint32_t count) {
        std::vector layouts(count, descriptorSetLayoutModelTransform);
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = count;
        allocInfo.pSetLayouts = layouts.data();
        std::vector<VkDescriptorSet> descriptorSets(count);
        vkAllocateDescriptorSets(device, &allocInfo, descriptorSets.data());

        {
            std::vector<VkWriteDescriptorSet> writes;
            std::vector<VkDescriptorBufferInfo> bufferInfos;
            writes.reserve(count);
            bufferInfos.reserve(count);
            for (uint32_t i = 0; i < count; i++) {
                VkDescriptorBufferInfo bufferInfo{};
                bufferInfo.buffer = modelTransformBuffer;
                bufferInfo.offset = i * sizeof(ModelTransform);
                bufferInfo.range = sizeof(ModelTransform);

                bufferInfos.push_back(bufferInfo);
                writes.push_back(VkWriteDescriptorSet{
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = descriptorSets[i],
                    .dstBinding = 0,
                    .dstArrayElement = 0,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .descriptorCount = 1,
                    .pBufferInfo = bufferInfos.data() + i,
                });
            };
            vkUpdateDescriptorSets(device, writes.size(), writes.data(), 0, nullptr);
        }
        return descriptorSets;
    }

    VkDescriptorSet createDescriptorSetViewProjection() {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayoutViewProjection;
        VkDescriptorSet descriptorSet;
        vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet);
        {
            VkDescriptorBufferInfo viewProjectionBufferInfo{
                .buffer = viewProjectionBuffer,
                .offset = 0,
                .range = sizeof(ViewProjection)
            };
            VkDescriptorBufferInfo lightBlockBufferInfo{
                .buffer = lightBlockBuffer,
                .offset = 0,
                .range = sizeof(LightBlock)
            };
            std::array writes = {
                VkWriteDescriptorSet{
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = descriptorSet,
                    .dstBinding = 0,
                    .dstArrayElement = 0,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .descriptorCount = 1,
                    .pBufferInfo = &viewProjectionBufferInfo,
                },
                VkWriteDescriptorSet{
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = descriptorSet,
                    .dstBinding = 1,
                    .dstArrayElement = 0,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .descriptorCount = 1,
                    .pBufferInfo = &lightBlockBufferInfo,
                },
            };
            vkUpdateDescriptorSets(device, writes.size(), writes.data(), 0, nullptr);
        }
        return descriptorSet;
    }

    static VkDescriptorSetLayout createDescriptorSetLayoutModelTransform(VkDevice device) {
        VkDescriptorSetLayoutBinding uboLayoutBinding{};
        uboLayoutBinding.binding = 0;
        uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboLayoutBinding.descriptorCount = 1;
        uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        std::array bindings = {uboLayoutBinding};
        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = bindings.size();
        layoutInfo.pBindings = bindings.data();

        VkDescriptorSetLayout descriptorSetLayout;
        vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout);
        return descriptorSetLayout;
    }

    static VkDescriptorSetLayout createDescriptorSetLayoutMaterial(VkDevice device) {
        VkDescriptorSetLayoutBinding samplerLayoutBinding{};
        samplerLayoutBinding.binding = 0;
        samplerLayoutBinding.descriptorCount = 1;
        samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerLayoutBinding.pImmutableSamplers = nullptr;
        samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding materialPropsBinding{};
        materialPropsBinding.binding = 1;
        materialPropsBinding.descriptorCount = 1;
        materialPropsBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        materialPropsBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        std::array bindings = {samplerLayoutBinding, materialPropsBinding};
        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = bindings.size();
        layoutInfo.pBindings = bindings.data();

        VkDescriptorSetLayout descriptorSetLayout;
        vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout);
        return descriptorSetLayout;
    }

    static VkDescriptorSetLayout createDescriptorSetLayoutViewProjection(VkDevice device) {
        std::array bindings = {
            VkDescriptorSetLayoutBinding{
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            },
            VkDescriptorSetLayoutBinding{
                .binding = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            }
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

    static VkDescriptorPool createDescriptorPool(VkDevice device, uint32_t poolSize) {
        std::array poolSizes = {
            VkDescriptorPoolSize{.type=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount=poolSize + poolSize + 2},
            VkDescriptorPoolSize{.type=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount=poolSize},
        };
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = poolSizes.size();
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = 3 * poolSize + 1;

        VkDescriptorPool descriptorPool;
        vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool);
        return descriptorPool;
    }

    static VkPipelineLayout createPipelineLayout(VkDevice device, std::vector<VkDescriptorSetLayout> const& descriptorSetLayout) {
        VkPipelineLayout pipelineLayout;
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = descriptorSetLayout.size();
        pipelineLayoutInfo.pSetLayouts = descriptorSetLayout.data();
        pipelineLayoutInfo.pushConstantRangeCount = 0; // No push constants
        if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create pipeline layout!");
        }
        return pipelineLayout;
    }

    static VkPipeline createPipeline(VkDevice device, VkExtent2D extent, VkRenderPass renderPass, VkPipelineLayout pipelineLayout) {
        VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
        vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertShaderStageInfo.module = createShaderModule(device, loadFile("build/shader.vertex.spv"));
        vertShaderStageInfo.pName = "main"; // Entry point in the shader

        VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
        fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragShaderStageInfo.module = createShaderModule(device, loadFile("build/shader.fragment.spv"));
        fragShaderStageInfo.pName = "main"; // Entry point in the shader

        VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0; // Binding index
        bindingDescription.stride = sizeof(Vertex); // Size of each vertex
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX; // Per-vertex data

        std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions = {};

        // Position attribute
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0; // Corresponds to inPosition in the vertex shader
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT; // vec3
        attributeDescriptions[0].offset = offsetof(Vertex, pos);

        // Normal attribute
        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1; // inNormal
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT; // vec3
        attributeDescriptions[1].offset = offsetof(Vertex, normal);

        // Color attribute
        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2; // Corresponds to inColor in the vertex shader
        attributeDescriptions[2].format = VK_FORMAT_R32G32B32_SFLOAT; // vec3
        attributeDescriptions[2].offset = offsetof(Vertex, color);

        // UV attribute
        attributeDescriptions[3].binding = 0;
        attributeDescriptions[3].location = 3; // Corresponds to inUV in the vertex shader
        attributeDescriptions[3].format = VK_FORMAT_R32G32_SFLOAT; // vec2
        attributeDescriptions[3].offset = offsetof(Vertex, uv);

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 1; // Number of bindings
        vertexInputInfo.pVertexBindingDescriptions = &bindingDescription; // Pointer to the binding descriptions
        vertexInputInfo.vertexAttributeDescriptionCount = attributeDescriptions.size(); // Number of attributes
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data(); // Pointer to the attribute descriptions

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(extent.width);
        viewport.height = static_cast<float>(extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset = { 0, 0 };
        scissor.extent = extent;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; // in framebuffer space
        rasterizer.depthBiasEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.blendEnable = VK_FALSE;
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.logicOp = VK_LOGIC_OP_COPY;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.minDepthBounds = 0.0f; // Optional
        depthStencil.maxDepthBounds = 1.0f; // Optional
        depthStencil.stencilTestEnable = VK_FALSE;
        depthStencil.front = {}; // Optional
        depthStencil.back = {}; // Optional

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0; // The index of the subpass in the render pass

        VkPipeline graphicsPipeline;
        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) != VK_SUCCESS) {
            throw std::runtime_error("failed to create graphics pipeline!");
        }

        // Cleanup shader modules after pipeline creation
        vkDestroyShaderModule(device, vertShaderStageInfo.module, nullptr);
        vkDestroyShaderModule(device, fragShaderStageInfo.module, nullptr);

        return graphicsPipeline;
    }

    static VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code) {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            throw std::runtime_error("failed to create shader module!");
        }

        return shaderModule;
    }

    static std::vector<char> loadFile(const std::string& filePath) {
        std::ifstream file(filePath, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("failed to open shader file!");
        }

        size_t fileSize = (size_t)file.tellg();
        std::vector<char> buffer(fileSize);
        file.seekg(0);
        file.read(buffer.data(), fileSize);
        file.close();

        return buffer;
    }
};
