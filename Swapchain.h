#pragma once

#include <vulkan/vulkan.h>
#include <iostream>

class Swapchain {
public:
    Swapchain(VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface, VkExtent2D extent, uint32_t imageCount, bool vsyncEnabled, std::unique_ptr<Swapchain> oldSwapchain = nullptr): 
        m_device(device), 
        m_extent(extent),
        m_imageCount(imageCount), 
        m_images(imageCount),
        m_imageAvailableSemaphores(imageCount),
        m_oldSwapchain(std::move(oldSwapchain))
    {
        // Get supported presentation modes
        // uint32_t presentModeCount;
        // vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);
        // std::vector<VkPresentModeKHR> presentModes(presentModeCount);
        // vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, presentModes.data());

        VkSurfaceCapabilitiesKHR surfaceCapabilities;
        {
            VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCapabilities);
            if (result != VK_SUCCESS) {
                throw std::runtime_error("Failed to get surface capabilities!");
            }
        }
        if (imageCount > surfaceCapabilities.maxImageCount || imageCount < surfaceCapabilities.minImageCount) {
            throw std::runtime_error("Unsopported swagchain image count!");
        }

        uint32_t formatCount;
        {
            VkResult result = vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
            if (result != VK_SUCCESS) {
                throw std::runtime_error("Failed to get surface formats!");
            }
        }
        std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, surfaceFormats.data());
        VkSurfaceFormatKHR selectedFormat;
        {
            bool found = false;
            for (const VkSurfaceFormatKHR& surfaceFormat : surfaceFormats) {
                if (surfaceFormat.format == VK_FORMAT_B8G8R8A8_SRGB && surfaceFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                    m_format = surfaceFormat.format;
                    selectedFormat = surfaceFormat;
                    found = true;
                    break;
                }
            }
            if (!found) {
                throw std::runtime_error("sRGB is not supported");
            }
        }
        
        VkSwapchainCreateInfoKHR swapchainInfo{};
        swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapchainInfo.surface = surface;
        swapchainInfo.minImageCount = imageCount;
        swapchainInfo.imageFormat = selectedFormat.format;
        swapchainInfo.imageColorSpace = selectedFormat.colorSpace;
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
            viewInfo.format = selectedFormat.format;
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

    VkFormat getFormat() const {
        return m_format;
    }

    uint32_t getImageCount() const {
        return m_imageCount;
    }

    VkImageView getImageView(int i) const {
        return m_imageViews[i];
    }

private:
    VkDevice m_device;
    VkSwapchainKHR m_swapchain;
    VkExtent2D m_extent;
    uint32_t m_imageCount;
    std::vector<VkImage> m_images;
    std::vector<VkImageView> m_imageViews;
    VkFormat m_format;
    std::vector<VkSemaphore> m_imageAvailableSemaphores;
    uint32_t m_currentFrame = 0;
    std::unique_ptr<Swapchain> m_oldSwapchain;
};
