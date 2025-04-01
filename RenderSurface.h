#pragma once

#include <vector>
#include <chrono>
#include <thread>
#include <vulkan/vulkan.h>
#include <vulkan/vk_enum_string_helper.h>

#include "Swapchain.h"
#include "VulkanFunctions.h"

/*
Manages:
- Swapchain
- Frames in flight
- Command buffers
- Presentation
- Rendering synchronization
- Render pass and framebuffers
*/
class RenderSurface {
public:
    struct CreateArgs {
        VkInstance instance;
        VkPhysicalDevice physicalDevice;
        VkDevice device;
        VkQueue graphicsQueue;
        VkQueue presentQueue;
        uint32_t graphicsQueueFamilyIndex;
        SDL_Window* window;
        VkExtent2D extent;
        uint32_t framesInFlight;
        bool vsyncEnabled;
        VkSampleCountFlagBits msaaSamples;
    };
    struct Frame {
        VkCommandBuffer commandBuffer;
        uint32_t swapchainImageIndex;
        VkSemaphore swapchainImageAvailableSemaphore;
    };

    RenderSurface(const CreateArgs& args): 
        m_instance(args.instance),
        m_device(args.device),
        m_physicalDevice(args.physicalDevice),
        m_window(args.window),
        m_graphicsQueue(args.graphicsQueue),
        m_presentQueue(args.presentQueue),
        m_framebuffers(args.framesInFlight),
        m_commandBuffers(args.framesInFlight),
        m_renderFinishedSemaphores(args.framesInFlight),
        m_renderFences(args.framesInFlight),
        m_framesInFlight(args.framesInFlight),
        m_vsyncEnabled(args.vsyncEnabled)
    {
        if (SDL_Vulkan_CreateSurface(m_window, m_instance, &m_surface) != SDL_TRUE) {
            std::cerr << SDL_GetError() << std::endl;
            throw std::runtime_error("Failed to create Vulkan surface");
        }

        m_swapchain = std::make_unique<Swapchain>(args.physicalDevice, args.device, m_surface, args.extent, args.framesInFlight, args.vsyncEnabled);
        createDepthImage(args.extent);
        createRenderPass();
        createFramebuffers();
        createCommandBuffers(args.graphicsQueueFamilyIndex);
        createSyncObjects();
    }
    ~RenderSurface() {
        // TODO destroy stuff
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
            VkClearValue{.color=clearColor},
            VkClearValue{.depthStencil={1.0f, 0}}
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
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &m_renderFinishedSemaphores[m_currentFrame]; // Wait for rendering to finish
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &m_swapchain->getHandle();
        presentInfo.pImageIndices = &frame.swapchainImageIndex;

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
    VkFormat getImageFormat() const { return m_swapchain->getFormat(); }
    VkFormat getDepthFormat() const { return m_depthFormat; }
    uint32_t getImageCount() const { return m_framesInFlight; }
    VkRenderPass getRenderPass() const { return m_renderPass; }
    
    // Config changes
    void setVsync(bool enabled) {
        if (enabled == m_vsyncEnabled) return;
        m_vsyncEnabled = enabled;
        recreateSwapchain();
    }
    void setMsaaSamples(VkSampleCountFlagBits samples) {}

private:
    VkInstance m_instance;
    VkPhysicalDevice m_physicalDevice;
    VkDevice m_device;
    SDL_Window* m_window;
    VkSurfaceKHR m_surface;
    VkQueue m_graphicsQueue;
    VkQueue m_presentQueue;
    VkRenderPass m_renderPass;
    std::vector<VkCommandBuffer> m_commandBuffers;
    VkCommandPool m_commandPool;
    std::unique_ptr<Swapchain> m_swapchain;
    bool m_vsyncEnabled;
    
    // Depth resources
    VkImage m_depthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_depthImageMemory = VK_NULL_HANDLE;
    VkImageView m_depthImageView = VK_NULL_HANDLE;
    VkFormat m_depthFormat;
    
    // MSAA resources
    // VkSampleCountFlagBits m_msaaSamples;
    // VkImage m_colorImage = VK_NULL_HANDLE;
    // VkDeviceMemory m_colorImageMemory = VK_NULL_HANDLE;
    // VkImageView m_colorImageView = VK_NULL_HANDLE;
    
    // Framebuffers
    std::vector<VkFramebuffer> m_framebuffers;
    
    // Synchronization
    std::vector<VkSemaphore> m_renderFinishedSemaphores;
    std::vector<VkFence> m_renderFences;
    uint32_t m_framesInFlight;
    uint32_t m_currentFrame = 0;

    void createDepthImage(VkExtent2D extent) {
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
            m_depthImageMemory
        );

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_depthImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_depthFormat;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_depthImageView) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create image views!");
        }
    }

    void createRenderPass() {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = m_swapchain->getFormat();
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; // Clear before use
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // Store after rendering
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentDescription depthAttachment{};
        depthAttachment.format = m_depthFormat;
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

        if (vkCreateRenderPass(m_device, &renderPassCreateInfo, nullptr, &m_renderPass) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create render pass!");
        }
    }

    void recreateSwapchain() {
        vkDeviceWaitIdle(m_device);
        for (size_t i = 0; i < m_framebuffers.size(); i++) {
            vkDestroyFramebuffer(m_device, m_framebuffers[i], nullptr);
        }
        int width;
        int height;
        SDL_GetWindowSize(m_window, &width, &height);
        VkExtent2D extent = {(uint32_t)width, (uint32_t)height};
        m_swapchain = std::make_unique<Swapchain>(m_physicalDevice, m_device, m_surface, extent, m_framesInFlight, m_vsyncEnabled, std::move(m_swapchain));
        createFramebuffers();
    }

    void createFramebuffers() {
        for (size_t i = 0; i < m_framebuffers.size(); i++) {
            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = m_renderPass;
            std::array attachments = {
                m_swapchain->getImageView(i),
                m_depthImageView,
            };
            framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
            framebufferInfo.pAttachments = attachments.data();
            framebufferInfo.width = m_swapchain->getExtent().width;
            framebufferInfo.height = m_swapchain->getExtent().height;
            framebufferInfo.layers = 1;
    
            if (vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_framebuffers[i]) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create framebuffer!");
            }
        }
    }

    void createCommandBuffers(uint32_t graphicsQueueFamilyIndex) {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = graphicsQueueFamilyIndex;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; // Enable reset command buffer
        if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create command pool!");
        }
        
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = m_commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; // Primary command buffers can be submitted to queues
        allocInfo.commandBufferCount = static_cast<uint32_t>(m_commandBuffers.size());
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
};
