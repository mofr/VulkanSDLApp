#pragma once

#include <array>
#include <span>
#include <vulkan/vulkan.h>

#include "Vertex.h"
#include "VulkanFunctions.h"
#include "FileFunctions.h"
#include "MeshObject.h"
#include "UniformBuffer.h"

/**
The class represents a concrete Vulkan pipeline to render textured meshes.
It requires a render pass with two attachments: color, depth.
It requires specific vertex format: Vertex.
Descriptor set layouts:
 Set 0: frame-level data
 Set 1: material data
  Binding 0: diffuse texture + sampler
  Binding 1: UBO material props
 Set 2: per-object data
  Binding 0: UBO with Model matrix
*/
class Pipeline {
public:
    explicit Pipeline(
        VkPhysicalDevice physicalDevice,
        VkDevice device, 
        VkExtent2D extent, 
        VkRenderPass renderPass, 
        VkSampleCountFlagBits msaaSamples,
        VkDescriptorSetLayout frameLevelDescriptorSetLayout,
        uint32_t poolSize
    ):
        m_physicalDevice(physicalDevice),
        m_device(device),
        m_modelTransforms(physicalDevice, device, poolSize),
        m_msaaSamples(msaaSamples),
        m_extent(extent),
        m_renderPass(renderPass)
    {
        m_descriptorSetLayoutMaterial = createDescriptorSetLayoutMaterial(device);
        m_descriptorSetLayoutModelTransform = createDescriptorSetLayoutModelTransform(device);
        m_layout = createPipelineLayout(device, {frameLevelDescriptorSetLayout, m_descriptorSetLayoutMaterial, m_descriptorSetLayoutModelTransform});
        m_pipeline = createPipeline(device, extent, renderPass, m_layout, msaaSamples);
        m_descriptorPool = createDescriptorPool(device, poolSize);
        m_modelTransformDescriptorSets = createDescriptorSetsModelTransforms(poolSize);
    }

    void updateRenderPass(VkRenderPass renderPass, VkSampleCountFlagBits msaaSamples) {
        m_msaaSamples = msaaSamples;
        m_renderPass = renderPass;
        vkDestroyPipeline(m_device, m_pipeline, nullptr);
        m_pipeline = createPipeline(m_device, m_extent, m_renderPass, m_layout, m_msaaSamples);
    }

    struct MaterialProps {
        glm::vec3 diffuseFactor{1.0f};
        float _padding1;
        glm::vec3 emitFactor{0.0f};
        float _padding2;
        float specularHardness = 1.0f;
        float specularPower = 1.0f;
    };

    void draw(
        VkCommandBuffer commandBuffer,
        VkDescriptorSet frameLevelDescriptorSet,
        std::vector<MeshObject> const& objects
    ) {
        for (size_t i = 0; i < objects.size(); ++i) {
            m_modelTransforms.data()[i] = {objects[i].getTransform()};
        }

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layout, 0, 1, &frameLevelDescriptorSet, 0, nullptr);

        // TODO group by vertex buffer and material
        for (uint32_t i = 0; i < objects.size(); i++) {
            auto const& object = objects[i];
            VkDescriptorSet transformDescriptorSet = m_modelTransformDescriptorSets[i];
            std::array descriptorSets = {object.materialDescriptorSet, transformDescriptorSet};
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layout, 1, descriptorSets.size(), descriptorSets.data(), 0, nullptr);
            VkBuffer vertexBuffers[] = { object.vertexBuffer };
            VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
            vkCmdDraw(commandBuffer, object.vertexCount, 1, 0, 0);
        }
    }

    VkDescriptorSet createMaterial(VkImageView textureImageView, VkSampler textureSampler, MaterialProps const& props) {
        // TODO need a proper Material object to handle the lifetime
        auto propsBuffer = new UniformBuffer<MaterialProps>(m_physicalDevice, m_device);
        propsBuffer->data() = props;
        VkDescriptorSet materialDescriptorSet = createMaterialDescriptorSet();
        {
            VkDescriptorImageInfo imageInfo {
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .imageView = textureImageView,
                .sampler = textureSampler,
            };
            VkDescriptorBufferInfo materialPropsBufferInfo = propsBuffer->descriptorBufferInfo();
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
            vkUpdateDescriptorSets(m_device, writes.size(), writes.data(), 0, nullptr);
        }
        return materialDescriptorSet;
    }

    ~Pipeline() {
    }

private:
    struct ModelTransform {
        glm::mat4 model;
    };

    VkDescriptorSet createMaterialDescriptorSet() const {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_descriptorSetLayoutMaterial;
        VkDescriptorSet descriptorSet;
        vkAllocateDescriptorSets(m_device, &allocInfo, &descriptorSet);
        return descriptorSet;
    }

    std::vector<VkDescriptorSet> createDescriptorSetsModelTransforms(uint32_t count) {
        std::vector layouts(count, m_descriptorSetLayoutModelTransform);
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_descriptorPool;
        allocInfo.descriptorSetCount = count;
        allocInfo.pSetLayouts = layouts.data();
        std::vector<VkDescriptorSet> descriptorSets(count);
        vkAllocateDescriptorSets(m_device, &allocInfo, descriptorSets.data());

        std::vector<VkWriteDescriptorSet> writes(count);
        std::vector<VkDescriptorBufferInfo> bufferInfos(count);
        for (uint32_t i = 0; i < count; i++) {
            bufferInfos[i] = m_modelTransforms.descriptorBufferInfo(i);
            writes[i] = VkWriteDescriptorSet{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = descriptorSets[i],
                .dstBinding = 0,
                .dstArrayElement = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .pBufferInfo = bufferInfos.data() + i,
            };
        };
        vkUpdateDescriptorSets(m_device, writes.size(), writes.data(), 0, nullptr);
        return descriptorSets;
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

    static VkPipeline createPipeline(VkDevice device, VkExtent2D extent, VkRenderPass renderPass, VkPipelineLayout pipelineLayout, VkSampleCountFlagBits rasterizationSamples) {
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

        std::array shaderStages = { vertShaderStageInfo, fragShaderStageInfo };

        std::array bindingDescriptions = {
                VkVertexInputBindingDescription{
                .binding = 0,
                .stride = sizeof(Vertex),
                .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
            },
        };

        std::array attributeDescriptions = {
            VkVertexInputAttributeDescription{
                .binding = 0,
                .location = 0,
                .format = VK_FORMAT_R32G32B32_SFLOAT,
                .offset = offsetof(Vertex, pos),
            },
            VkVertexInputAttributeDescription{
                .binding = 0,
                .location = 1,
                .format = VK_FORMAT_R32G32B32_SFLOAT,
                .offset = offsetof(Vertex, normal),
            },
            VkVertexInputAttributeDescription{
                .binding = 0,
                .location = 2,
                .format = VK_FORMAT_R32G32B32_SFLOAT,
                .offset = offsetof(Vertex, color),
            },
            VkVertexInputAttributeDescription{
                .binding = 0,
                .location = 3,
                .format = VK_FORMAT_R32G32_SFLOAT,
                .offset = offsetof(Vertex, uv),
            },
        };

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = bindingDescriptions.size();
        vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data();
        vertexInputInfo.vertexAttributeDescriptionCount = attributeDescriptions.size();
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

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
        multisampling.rasterizationSamples = rasterizationSamples;

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
        pipelineInfo.stageCount = shaderStages.size();
        pipelineInfo.pStages = shaderStages.data();
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

    VkPhysicalDevice m_physicalDevice;
    VkDevice m_device;

    VkPipelineLayout m_layout;
    VkPipeline m_pipeline;

    VkDescriptorPool m_descriptorPool;
    VkDescriptorSetLayout m_descriptorSetLayoutModelTransform;
    VkDescriptorSetLayout m_descriptorSetLayoutMaterial;
    std::vector<VkDescriptorSet> m_modelTransformDescriptorSets;
    UniformBuffer<ModelTransform[]> m_modelTransforms;

    VkSampleCountFlagBits m_msaaSamples;
    VkExtent2D m_extent;
    VkRenderPass m_renderPass;
};
