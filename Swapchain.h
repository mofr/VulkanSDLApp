#pragma once

#include <vulkan/vulkan.h>
#include <iostream>

class Swapchain {
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    VkExtent2D extent;
    uint32_t imageCount;
    std::vector<VkImage> images;
    std::vector<VkImageView> imageViews;
    VkFormat format;
    std::vector<VkSemaphore> imageAvailableSemaphores;
    uint32_t currentFrame = 0;

public:
    Swapchain(VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface, VkExtent2D extent, uint32_t imageCount): 
        physicalDevice(physicalDevice),
        device(device), 
        surface(surface),
        extent(extent),
        imageCount(imageCount), 
        images(imageCount),
        imageAvailableSemaphores(imageCount)
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
                    format = surfaceFormat.format;
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
        swapchainInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR; // Vsync
        swapchainInfo.clipped = VK_TRUE;
        swapchainInfo.oldSwapchain = VK_NULL_HANDLE; // For the initial swapchain
    
        if (vkCreateSwapchainKHR(device, &swapchainInfo, nullptr, &swapchain) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create swapchain");
        }
    
        vkGetSwapchainImagesKHR(device, swapchain, &imageCount, images.data());

        imageViews.resize(images.size());
        for (size_t i = 0; i < images.size(); i++) {
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = images[i];
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
    
            if (vkCreateImageView(device, &viewInfo, nullptr, &imageViews[i]) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create image views!");
            }
        }

        VkSemaphoreCreateInfo semaphoreInfo{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        };
        for (size_t i = 0; i < imageCount; ++i) {
            vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]);
        }
    }

    std::tuple<uint32_t, VkSemaphore> acquireNextImage() {
        VkSemaphore semaphore = imageAvailableSemaphores[currentFrame];
        uint32_t imageIndex;
        VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, semaphore, VK_NULL_HANDLE, &imageIndex);
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            // TODO recreateSwapChain();
            throw std::runtime_error("swapchain is out of date and needs to be recreated");
        } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            throw std::runtime_error("failed to acquire swap chain image!");
        }
        currentFrame = (currentFrame + 1) % imageCount;
        return {imageIndex, semaphore};
    }

    VkSwapchainKHR const& getHandle() const {
        return swapchain;
    }

    VkExtent2D getExtent() const {
        return extent;
    }

    VkFormat getFormat() const {
        return format;
    }

    uint32_t getImageCount() const {
        return imageCount;
    }

    VkImageView getImageView(int i) const {
        return imageViews[i];
    }
};
