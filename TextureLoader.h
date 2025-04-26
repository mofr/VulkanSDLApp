#pragma once

#include <vector>
#include <iostream>
#include <vulkan/vulkan.h>
#include <ktxvulkan.h>

class TextureLoader {
public:
    TextureLoader(
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        VkQueue queue,
        VkCommandPool commandPool
    ) {
        ktxVulkanDeviceInfo_Construct(
            &m_deviceInfo,
            physicalDevice,
            device,
            queue,
            commandPool,
            nullptr
        );
    }

    VkImageView loadKtx(const char* fileName) {
        ktxTexture* kTexture;
        ktxVulkanTexture texture;
        KTX_error_code result;
        result = ktxTexture_CreateFromNamedFile(
            fileName,
            KTX_TEXTURE_CREATE_NO_FLAGS,
            &kTexture
        );
        if (result != KTX_SUCCESS) {
            throw std::runtime_error("ktxTexture_CreateFromNamedFile failed");
        }

        result = ktxTexture_VkUploadEx(
            kTexture, &m_deviceInfo, &texture,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        );
        if (result != KTX_SUCCESS) {
            std::cerr << "ktxTexture_VkUploadEx failed: " << ktxErrorString(result) << std::endl;
            throw std::runtime_error("ktxTexture_VkUploadEx failed");
        }

        ktxTexture_Destroy(kTexture);
        
        VkImageViewCreateInfo viewInfo {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = texture.image,
            .viewType = texture.viewType,
            .format = texture.imageFormat,
            .subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .subresourceRange.baseMipLevel = 0,
            .subresourceRange.levelCount = texture.levelCount,
            .subresourceRange.baseArrayLayer = 0,
            .subresourceRange.layerCount = texture.layerCount,
        };
        VkImageView imageView;
        if (vkCreateImageView(m_deviceInfo.device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
            throw std::runtime_error("failed to create texture image view!");
        }
        m_imageViews.push_back(imageView);
        return imageView;
    }

    ~TextureLoader() {
        for (auto imageView : m_imageViews) {
            vkDestroyImageView(m_deviceInfo.device, imageView, nullptr);
        }
        for (auto texture : m_ktxTextures) {
            ktxVulkanTexture_Destruct(&texture, m_deviceInfo.device, nullptr);
        }
        ktxVulkanDeviceInfo_Destruct(&m_deviceInfo);
    }

private:
    ktxVulkanDeviceInfo m_deviceInfo;
    std::vector<VkImageView> m_imageViews;
    std::vector<ktxVulkanTexture> m_ktxTextures;
};
