#pragma once

#include <vulkan/vulkan.h>
#include <iostream>

#include "SurfaceFormatSet.h"

class Swapchain {
public:
    Swapchain(
        VkPhysicalDevice physicalDevice, 
        VkDevice device, 
        VkSurfaceKHR surface, 
        VkExtent2D extent, 
        uint32_t imageCount, 
        bool vsyncEnabled, 
        std::vector<VkSurfaceFormatKHR> preferredFormats, 
        std::unique_ptr<Swapchain> oldSwapchain = nullptr
    ): 
        m_device(device), 
        m_extent(extent),
        m_imageCount(imageCount), 
        m_images(imageCount),
        m_imageAvailableSemaphores(imageCount),
        m_oldSwapchain(std::move(oldSwapchain))
    {
        {
            VkSurfaceCapabilitiesKHR surfaceCapabilities;
            if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCapabilities) != VK_SUCCESS) {
                throw std::runtime_error("Failed to get surface capabilities!");
            }
            if (imageCount > surfaceCapabilities.maxImageCount || imageCount < surfaceCapabilities.minImageCount) {
                throw std::runtime_error("Unsupported swagchain image count!");
            }
        }

        m_supportedFormats = getSupportedFormats(physicalDevice, surface);
        m_surfaceFormat = chooseSurfaceFormat(preferredFormats, m_supportedFormats);

        VkSwapchainCreateInfoKHR swapchainInfo{};
        swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapchainInfo.surface = surface;
        swapchainInfo.minImageCount = imageCount;
        swapchainInfo.imageFormat = m_surfaceFormat.format;
        swapchainInfo.imageColorSpace = m_surfaceFormat.colorSpace;
        swapchainInfo.imageExtent = extent;
        swapchainInfo.imageArrayLayers = 1;
        swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swapchainInfo.queueFamilyIndexCount = 0;
        swapchainInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; // Adjust for windowed mode if needed
        swapchainInfo.presentMode = vsyncEnabled ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_IMMEDIATE_KHR;
        swapchainInfo.clipped = VK_TRUE;
        swapchainInfo.oldSwapchain = m_oldSwapchain ? m_oldSwapchain->m_swapchain : nullptr;

        if (vkCreateSwapchainKHR(device, &swapchainInfo, nullptr, &m_swapchain) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create swapchain");
        }
    
        vkGetSwapchainImagesKHR(device, m_swapchain, &imageCount, m_images.data());

        m_imageViews.resize(m_images.size());
        for (size_t i = 0; i < m_images.size(); i++) {
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = m_images[i];
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = m_surfaceFormat.format;
            viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;
    
            if (vkCreateImageView(device, &viewInfo, nullptr, &m_imageViews[i]) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create image views!");
            }
        }

        VkSemaphoreCreateInfo semaphoreInfo{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        };
        for (size_t i = 0; i < imageCount; ++i) {
            vkCreateSemaphore(device, &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]);
        }
    }

    ~Swapchain() {
        for (size_t i = 0; i < m_imageViews.size(); i++) {
            vkDestroyImageView(m_device, m_imageViews[i], nullptr);
        }
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
    }

    std::tuple<bool, uint32_t, VkSemaphore> acquireNextImage() {
        VkSemaphore semaphore = m_imageAvailableSemaphores[m_currentFrame];
        uint32_t imageIndex;
        VkResult result = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX, semaphore, VK_NULL_HANDLE, &imageIndex);
        if (result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_OUT_OF_DATE_KHR) {
            return {true, 0, nullptr};
        }
        if (result != VK_SUCCESS) {
            throw std::runtime_error("failed to acquire swap chain image!");
        }
        m_currentFrame = (m_currentFrame + 1) % m_imageCount;
        return {false, imageIndex, semaphore};
    }

    VkSwapchainKHR const& getHandle() const {
        return m_swapchain;
    }

    VkExtent2D getExtent() const {
        return m_extent;
    }

    VkSurfaceFormatKHR getFormat() const {
        return m_surfaceFormat;
    }

    uint32_t getImageCount() const {
        return m_imageCount;
    }

    VkImageView getImageView(int i) const {
        return m_imageViews[i];
    }

    const SurfaceFormatSet& getSupportedFormats() const {
        return m_supportedFormats;
    }

private:
    static SurfaceFormatSet getSupportedFormats(
        VkPhysicalDevice physicalDevice,
        VkSurfaceKHR surface
    ) {
        uint32_t formatCount;
        if (vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr) != VK_SUCCESS) {
            throw std::runtime_error("Failed to get surface format count!");
        }
        std::vector<VkSurfaceFormatKHR> supportedFormats(formatCount);
        if (vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, supportedFormats.data()) != VK_SUCCESS) {
            throw std::runtime_error("Failed to get surface formats!");
        }
        return SurfaceFormatSet(supportedFormats.begin(), supportedFormats.end());
    }

    static VkSurfaceFormatKHR chooseSurfaceFormat(
        std::vector<VkSurfaceFormatKHR> preferredFormats,
        SurfaceFormatSet supportedFormats
    ) {
        for (const VkSurfaceFormatKHR& preferredFormat : preferredFormats) {
            if (supportedFormats.contains(preferredFormat)) {
                return preferredFormat;
            }
        }
        throw std::runtime_error("No suitable surface format available");
    }

    VkDevice m_device;
    VkSwapchainKHR m_swapchain;
    VkExtent2D m_extent;
    uint32_t m_imageCount;
    std::vector<VkImage> m_images;
    std::vector<VkImageView> m_imageViews;
    VkSurfaceFormatKHR m_surfaceFormat;
    std::vector<VkSemaphore> m_imageAvailableSemaphores;
    uint32_t m_currentFrame = 0;
    std::unique_ptr<Swapchain> m_oldSwapchain;
    SurfaceFormatSet m_supportedFormats;
};
