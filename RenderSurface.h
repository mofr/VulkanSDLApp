#pragma once

#include <vector>
#include <chrono>
#include <thread>
#include <vulkan/vulkan.h>
#include <vulkan/vk_enum_string_helper.h>

#include "Swapchain.h"
#include "VulkanFunctions.h"
#include "Tonemapper.h"

/*
Manages:
- Swapchain
- Command buffers
- Frames in flight synchronization
- Render passes and framebuffers
- Presentation (including tonemapping)
*/
class RenderSurface {
public:
    struct CreateArgs {
        VkInstance instance;
        VkPhysicalDevice physicalDevice;
        VkDevice device;
        std::vector<VkSurfaceFormatKHR> preferredSurfaceFormats;
        VkFormat renderInFormat;
        VkQueue graphicsQueue;
        VkQueue presentQueue;
        uint32_t graphicsQueueFamilyIndex;
        SDL_Window* window;
        uint32_t framesInFlight;
        bool vsyncEnabled;
        VkSampleCountFlagBits msaaSamples;
        VkDescriptorSetLayout frameLevelDescriptorSetLayout;
    };
    struct Frame {
        VkCommandBuffer commandBuffer;
        uint32_t swapchainImageIndex;
        VkSemaphore swapchainImageAvailableSemaphore;
    };

    RenderSurface(const CreateArgs& args): 
        m_instance(args.instance),
        m_physicalDevice(args.physicalDevice),
        m_device(args.device),
        m_window(args.window),
        m_preferredSurfaceFormats(args.preferredSurfaceFormats),
        m_graphicsQueue(args.graphicsQueue),
        m_presentQueue(args.presentQueue),
        m_vsyncEnabled(args.vsyncEnabled),
        m_frameLevelDescriptorSetLayout(args.frameLevelDescriptorSetLayout),
        m_colorImageFormat(args.renderInFormat),
        m_msaaSamples(args.msaaSamples),
        m_framesInFlight(args.framesInFlight),
        m_framebuffers(args.framesInFlight),
        m_renderFinishedSemaphores(args.framesInFlight),
        m_renderFences(args.framesInFlight),
        m_commandBuffers(args.framesInFlight)
    {
        if (SDL_Vulkan_CreateSurface(m_window, m_instance, &m_surface) != SDL_TRUE) {
            std::cerr << SDL_GetError() << std::endl;
            throw std::runtime_error("Failed to create Vulkan surface");
        }
        VkExtent2D extent = getWindowExtent();
        m_swapchain = std::make_unique<Swapchain>(args.physicalDevice, args.device, m_surface, extent, args.framesInFlight, args.vsyncEnabled, m_preferredSurfaceFormats);
        createSyncObjects();
        createCommandBuffers(args.graphicsQueueFamilyIndex);
        createImages(extent);
        createRenderPass();
    }

    ~RenderSurface() {
        destroyImages();
        // TODO destroy all resources
    }

    // Disable copying
    RenderSurface(const RenderSurface&) = delete;
    RenderSurface& operator=(const RenderSurface&) = delete;

    Frame beginFrame(VkClearColorValue clearColor = {}) {
        vkWaitForFences(m_device, 1, &m_renderFences[m_currentFrame], true, UINT64_MAX);
        uint32_t swapchainImageIndex;
        VkSemaphore swapchainImageAvailableSemaphore;
        while (true) {
            auto [needRecreateSwapchain, _swapchainImageIndex, _swapchainImageAvailableSemaphore] = m_swapchain->acquireNextImage();
            if (needRecreateSwapchain) {
                recreateSwapchain();
                continue;
            }
            swapchainImageIndex = _swapchainImageIndex;
            swapchainImageAvailableSemaphore = _swapchainImageAvailableSemaphore;
            break;
        }

        vkResetFences(m_device, 1, &m_renderFences[m_currentFrame]);

        VkCommandBuffer commandBuffer = m_commandBuffers[m_currentFrame];
        vkResetCommandBuffer(commandBuffer, 0);
        VkCommandBufferBeginInfo commanBufferBeginInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        };
        if (vkBeginCommandBuffer(commandBuffer, &commanBufferBeginInfo) != VK_SUCCESS) {
            throw std::runtime_error("Failed to begin recording command buffer!");
        }

        std::array clearValues{
            VkClearValue{.depthStencil={1.0f, 0}},
            VkClearValue{.color=clearColor},
            VkClearValue{.color=clearColor},
        };
        VkRenderPassBeginInfo renderPassBeginInfo{
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = m_renderPass,
            .framebuffer = m_framebuffers[swapchainImageIndex],
            .renderArea.offset = { 0, 0 },
            .renderArea.extent = m_swapchain->getExtent(),
            .clearValueCount = clearValues.size(),
            .pClearValues = clearValues.data(),
        };
        vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

        return {commandBuffer, swapchainImageIndex, swapchainImageAvailableSemaphore};
    }

    void postprocess(Frame frame, VkDescriptorSet frameLevelDescriptorSet) {
        vkCmdNextSubpass(frame.commandBuffer, VK_SUBPASS_CONTENTS_INLINE);
        m_tonemapper->tonemap(
            frame.commandBuffer,
            frameLevelDescriptorSet,
            m_tonemapOperator,
            m_exposure,
            m_reinhardWhitePoint
        );
    }

    void endFrame(Frame frame) {
        vkCmdEndRenderPass(frame.commandBuffer);

        if (vkEndCommandBuffer(frame.commandBuffer) != VK_SUCCESS) {
            throw std::runtime_error("Failed to record command buffer!");
        }

        // Submit the command buffer
        VkPipelineStageFlags waitStages[] = {
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT // Wait for color output stage
        };
        VkSubmitInfo submitInfo{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &frame.swapchainImageAvailableSemaphore, // Wait for the image to be available
            .pWaitDstStageMask = waitStages,
            .commandBufferCount = 1,
            .pCommandBuffers = &frame.commandBuffer,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &m_renderFinishedSemaphores[m_currentFrame], // Signal when rendering is finished
        };
        if (vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_renderFences[m_currentFrame]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to submit draw command buffer!");
        }

        // Present the rendered image
        VkPresentInfoKHR presentInfo {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &m_renderFinishedSemaphores[m_currentFrame], // Wait for rendering to finish
            .swapchainCount = 1,
            .pSwapchains = &m_swapchain->getHandle(),
            .pImageIndices = &frame.swapchainImageIndex,
        };

        VkResult presentResult = vkQueuePresentKHR(m_presentQueue, &presentInfo);
        if (presentResult == VK_SUBOPTIMAL_KHR) {
            recreateSwapchain();
        }
        else if (presentResult != VK_SUCCESS) {
            std::cout << string_VkResult(presentResult) << std::endl;
            throw std::runtime_error("Failed to present swapchain image!");
        }

        m_currentFrame = (m_currentFrame + 1) % m_framesInFlight;
    }

    // Getters
    VkExtent2D getExtent() const { return m_swapchain->getExtent(); }
    VkSurfaceFormatKHR getFormat() const { return m_swapchain->getFormat(); }
    VkFormat getImageFormat() const { return m_swapchain->getFormat().format; }
    VkFormat getDepthFormat() const { return m_depthFormat; }
    uint32_t getImageCount() const { return m_framesInFlight; }
    VkRenderPass getRenderPass() const { return m_renderPass; }
    VkSampleCountFlagBits getMsaaSamples() const { return m_msaaSamples; }
    uint32_t getFramesInFlight() const { return m_framesInFlight; }
    bool isFormatSupported(VkSurfaceFormatKHR surfaceFormat) const {
        return m_swapchain->getSupportedFormats().contains(surfaceFormat);
    }
    
    // Config changes
    void setVsync(bool enabled) {
        if (enabled == m_vsyncEnabled) return;
        m_vsyncEnabled = enabled;
        recreateSwapchain();
    }

    void setMsaaSamples(VkSampleCountFlagBits samples) {
        if (samples == m_msaaSamples) return;
        m_msaaSamples = samples;
        recreateSwapchain();
    }

    void setDisplayFormat(VkSurfaceFormatKHR format) {
        m_preferredSurfaceFormats.clear();
        m_preferredSurfaceFormats.push_back(format);
        recreateSwapchain();
    }

    void setTonemappingParameters(Tonemapper::Operator op, float exposure, float reinhardWhitePoint = 1.0f) {
        m_tonemapOperator = op;
        m_exposure = exposure;
        m_reinhardWhitePoint = reinhardWhitePoint;
    }

private:
    void createImages(VkExtent2D extent) {
        m_depthFormat = findDepthFormat(m_physicalDevice);
        createImage(
            m_physicalDevice,
            m_device,
            extent.width,
            extent.height,
            m_depthFormat,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            m_depthImage,
            m_depthImageMemory,
            m_msaaSamples
        );
        VkImageViewCreateInfo createDepthImageViewInfo {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = m_depthImage,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = m_depthFormat,
            .subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .subresourceRange.baseMipLevel = 0,
            .subresourceRange.levelCount = 1,
            .subresourceRange.baseArrayLayer = 0,
            .subresourceRange.layerCount = 1,
        };
        if (vkCreateImageView(m_device, &createDepthImageViewInfo, nullptr, &m_depthImageView) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create depth image view!");
        }

        createImage(
            m_physicalDevice,
            m_device,
            extent.width,
            extent.height,
            m_colorImageFormat,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            m_colorImage,
            m_colorImageMemory
        );
        VkImageViewCreateInfo createColorImageViewInfo {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = m_colorImage,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = m_colorImageFormat,
            .subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .subresourceRange.baseMipLevel = 0,
            .subresourceRange.levelCount = 1,
            .subresourceRange.baseArrayLayer = 0,
            .subresourceRange.layerCount = 1,
        };
        if (vkCreateImageView(m_device, &createColorImageViewInfo, nullptr, &m_colorImageView) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create color image view!");
        }

        if (m_msaaSamples > 1) {
            createImage(
                m_physicalDevice,
                m_device,
                extent.width,
                extent.height,
                m_colorImageFormat,
                VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                m_multisampledColorImage,
                m_multisampledColorImageMemory,
                m_msaaSamples
            );
            VkImageViewCreateInfo createMultisampledColorImageViewInfo {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = m_multisampledColorImage,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = m_colorImageFormat,
                .subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .subresourceRange.baseMipLevel = 0,
                .subresourceRange.levelCount = 1,
                .subresourceRange.baseArrayLayer = 0,
                .subresourceRange.layerCount = 1,
            };
            if (vkCreateImageView(m_device, &createMultisampledColorImageViewInfo, nullptr, &m_multisampledColorImageView) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create color image view!");
            }
        }
    }

    void destroyImages() {
        if (m_multisampledColorImageView) {
            vkDestroyImageView(m_device, m_multisampledColorImageView, nullptr);
            vkDestroyImage(m_device, m_multisampledColorImage, nullptr);
            m_multisampledColorImageView = nullptr;
            m_multisampledColorImage = nullptr;
        }
        vkDestroyImageView(m_device, m_depthImageView, nullptr);
        vkDestroyImage(m_device, m_depthImage, nullptr);
        vkDestroyImageView(m_device, m_colorImageView, nullptr);
        vkDestroyImage(m_device, m_colorImage, nullptr);
    }

    void createRenderPass() {
        std::vector<VkAttachmentDescription> attachments;

        uint32_t depthAttachment = attachments.size();
        attachments.push_back(
            {
                .format = m_depthFormat,
                .samples = m_msaaSamples,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            }
        );

        uint32_t colorAttachment = attachments.size();
        attachments.push_back(
            {
                .format = m_colorImageFormat,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            }
        );

        uint32_t outputAttachment = attachments.size();
        attachments.push_back(
            {
                .format = m_swapchain->getFormat().format,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            }
        );

        uint32_t msaaAttachment = attachments.size();
        if (m_msaaSamples > 1) {
            attachments.push_back({
                .format = m_colorImageFormat,
                .samples = m_msaaSamples,
                .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            });
        }
        VkAttachmentReference depthAttachmentRef {
            .attachment = depthAttachment,
            .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        };
        VkAttachmentReference colorAttachmentRef {
            .attachment = colorAttachment,
            .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        };
        VkAttachmentReference postProcessInputRef {
            .attachment = colorAttachment,
            .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        VkAttachmentReference outputAttachmentRef {
            .attachment = outputAttachment,
            .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        };
        VkAttachmentReference msaaAttachmentRef {
            .attachment = msaaAttachment,
            .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        };
        std::array subpasses = {
            VkSubpassDescription {
                .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                .colorAttachmentCount = 1,
                .pColorAttachments = m_msaaSamples > 1 ? &msaaAttachmentRef : &colorAttachmentRef,
                .pResolveAttachments = m_msaaSamples > 1 ? &colorAttachmentRef : nullptr,
                .pDepthStencilAttachment = &depthAttachmentRef,
            },
            VkSubpassDescription {
                .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                .inputAttachmentCount = 1,
                .pInputAttachments = &postProcessInputRef,
                .colorAttachmentCount = 1,
                .pColorAttachments = &outputAttachmentRef,
            },
        };
        std::array dependencies = {
            VkSubpassDependency {
                .srcSubpass = VK_SUBPASS_EXTERNAL,
                .dstSubpass = 0,
                .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            },
            VkSubpassDependency {
                .srcSubpass = 0,
                .dstSubpass = 1,
                .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT,
            },
            VkSubpassDependency {
                .srcSubpass = 1,
                .dstSubpass = VK_SUBPASS_EXTERNAL,
                .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .dstAccessMask = 0,
            },
        };
        VkRenderPassCreateInfo renderPassCreateInfo {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .attachmentCount = static_cast<uint32_t>(attachments.size()),
            .pAttachments = attachments.data(),
            .subpassCount = static_cast<uint32_t>(subpasses.size()),
            .pSubpasses = subpasses.data(),
            .dependencyCount = dependencies.size(),
            .pDependencies = dependencies.data(),
        };
        if (vkCreateRenderPass(m_device, &renderPassCreateInfo, nullptr, &m_renderPass) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create render pass!");
        }

        VkExtent2D extent = m_swapchain->getExtent();
        std::vector<VkImageView> framebufferAttachments;
        for (size_t i = 0; i < m_framebuffers.size(); i++) {
            framebufferAttachments = {
                m_depthImageView,
                m_colorImageView,
                m_swapchain->getImageView(i),
            };
            if (m_msaaSamples > 1) {
                framebufferAttachments.push_back(m_multisampledColorImageView);
            }
            VkFramebufferCreateInfo framebufferInfo {
                .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                .renderPass = m_renderPass,
                .attachmentCount = static_cast<uint32_t>(framebufferAttachments.size()),
                .pAttachments = framebufferAttachments.data(),
                .width = extent.width,
                .height = extent.height,
                .layers = 1,
            };
            if (vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_framebuffers[i]) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create framebuffer!");
            }
        }

        m_tonemapper = std::make_unique<Tonemapper>(m_device, m_renderPass, 1, extent, m_frameLevelDescriptorSetLayout, m_colorImageView);
    }

    void recreateSwapchain() {
        vkDeviceWaitIdle(m_device);
        for (size_t i = 0; i < m_framebuffers.size(); i++) {
            vkDestroyFramebuffer(m_device, m_framebuffers[i], nullptr);
        }
        destroyImages();
        VkExtent2D extent = getWindowExtent();
        m_swapchain = std::make_unique<Swapchain>(m_physicalDevice, m_device, m_surface, extent, m_framesInFlight, m_vsyncEnabled, m_preferredSurfaceFormats, std::move(m_swapchain));
        createImages(extent);

        vkDestroyRenderPass(m_device, m_renderPass, nullptr);
        createRenderPass();
    }

    void createCommandBuffers(uint32_t graphicsQueueFamilyIndex) {
        VkCommandPoolCreateInfo poolInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .queueFamilyIndex = graphicsQueueFamilyIndex,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        };
        if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create command pool!");
        }
        
        VkCommandBufferAllocateInfo allocInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = m_commandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = static_cast<uint32_t>(m_commandBuffers.size()),
        };
        if (vkAllocateCommandBuffers(m_device, &allocInfo, m_commandBuffers.data()) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate command buffers!");
        }
    }

    void createSyncObjects() {
        VkSemaphoreCreateInfo semaphoreInfo{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
        };
        VkFenceCreateInfo fenceInfo{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };
        for (size_t i = 0; i < m_framesInFlight; ++i) {
            vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]);
            vkCreateFence(m_device, &fenceInfo, nullptr, &m_renderFences[i]);
        }
    }

    VkExtent2D getWindowExtent() const {
        int width;
        int height;
        SDL_GetWindowSize(m_window, &width, &height);
        return {(uint32_t)width, (uint32_t)height};
    }

    VkInstance m_instance;
    VkPhysicalDevice m_physicalDevice;
    VkDevice m_device;
    SDL_Window* m_window;
    VkSurfaceKHR m_surface;
    std::vector<VkSurfaceFormatKHR> m_preferredSurfaceFormats;
    VkQueue m_graphicsQueue;
    VkQueue m_presentQueue;
    VkRenderPass m_renderPass;
    VkCommandPool m_commandPool;
    bool m_vsyncEnabled;
    std::unique_ptr<Swapchain> m_swapchain;
    VkDescriptorSetLayout m_frameLevelDescriptorSetLayout;

    // Tone mapping
    std::unique_ptr<Tonemapper> m_tonemapper;
    Tonemapper::Operator m_tonemapOperator = Tonemapper::Operator::NoTonemapping;
    float m_exposure = 1.0f;
    float m_reinhardWhitePoint = 1.0f;

    // Image before tonemapping
    VkImage m_colorImage;
    VkDeviceMemory m_colorImageMemory;
    VkImageView m_colorImageView;
    VkFormat m_colorImageFormat;
    
    // Depth resources
    VkImage m_depthImage;
    VkDeviceMemory m_depthImageMemory;
    VkImageView m_depthImageView;
    VkFormat m_depthFormat;
    
    // MSAA resources
    VkSampleCountFlagBits m_msaaSamples;
    VkImage m_multisampledColorImage;
    VkDeviceMemory m_multisampledColorImageMemory;
    VkImageView m_multisampledColorImageView;
    
    // Framebuffers
    uint32_t m_framesInFlight;
    uint32_t m_currentFrame = 0;
    std::vector<VkFramebuffer> m_framebuffers;
    std::vector<VkSemaphore> m_renderFinishedSemaphores;
    std::vector<VkFence> m_renderFences;
    std::vector<VkCommandBuffer> m_commandBuffers;
};
