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

    ~TextureLoader() {
        for (auto imageView : m_imageViews) {
            vkDestroyImageView(m_deviceInfo.device, imageView, nullptr);
        }
        for (auto image : m_images) {
            vkDestroyImage(m_deviceInfo.device, image, nullptr);
        }
        for (auto texture : m_ktxTextures) {
            ktxVulkanTexture_Destruct(&texture, m_deviceInfo.device, nullptr);
        }
        ktxVulkanDeviceInfo_Destruct(&m_deviceInfo);
    }

    VkImageView loadKtx(const char* fileName) {
        PROFILE_ME;
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
            kTexture, 
            &m_deviceInfo,
            &texture,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        );
        if (result != KTX_SUCCESS) {
            std::cerr << "ktxTexture_VkUploadEx failed: " << ktxErrorString(result) << std::endl;
            std::cerr << "File: " << fileName << std::endl;
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

    VkImageView loadCubemap(
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        VkCommandPool commandPool,
        VkQueue queue,
        std::array<std::string, 6> filenames
    ) {
        PROFILE_ME_AS("loadCubemap");
        std::array imageDatas = {
            loadImage(filenames[0]),
            loadImage(filenames[1]),
            loadImage(filenames[2]),
            loadImage(filenames[3]),
            loadImage(filenames[4]),
            loadImage(filenames[5]),
        };
        // TODO check that all images are of the same size
        uint32_t dataSize = imageDatas[0].dataSize * 6;  // TODO sum instead
        uint32_t width = imageDatas[0].width;
        uint32_t height = imageDatas[0].height;
        std::array<VkDeviceSize, 6> offsets;
        VkFormat imageFormat = imageDatas[0].imageFormat;

        VkImage textureImage;
        VkDeviceMemory textureImageMemory;
        uint32_t mipLevels = 1;
        createImage(
            physicalDevice,
            device,
            width,
            height,
            imageFormat,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            textureImage,
            textureImageMemory,
            VK_SAMPLE_COUNT_1_BIT,
            mipLevels,
            6,
            VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT
        );

        VkDeviceMemory stagingBufferMemory;
        VkBuffer stagingBuffer = createBuffer(device, physicalDevice, dataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingBufferMemory);
        {
            void* data;
            intptr_t offset = 0;
            vkMapMemory(device, stagingBufferMemory, 0, dataSize, 0, &data);
            for (size_t i = 0; i < imageDatas.size(); ++i) {
                auto const& imageData = imageDatas[i];
                char* dst = (char*)data + offset;
                memcpy(dst, imageData.data.get(), imageData.dataSize);
                offsets[i] = offset;
                offset += imageData.dataSize;
            }
            vkUnmapMemory(device, stagingBufferMemory);
        }

        VkCommandBuffer commandBuffer = beginSingleTimeCommands(device, commandPool);

        transitionImageLayout(commandBuffer, textureImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, 1, 6);
        std::array<VkBufferImageCopy, 6> regions;
        for (size_t i = 0; i < regions.size(); ++i) {
            regions[i] = VkBufferImageCopy{
                .bufferOffset = offsets[i],
                .imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .imageSubresource.mipLevel = 0,
                .imageSubresource.baseArrayLayer = static_cast<uint32_t>(i),
                .imageSubresource.layerCount = 1,
                .imageOffset = {0, 0, 0},
                .imageExtent = {width, height, 1},
            };
        };
        
        vkCmdCopyBufferToImage(
            commandBuffer,
            stagingBuffer,
            textureImage,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            regions.size(),
            regions.data()
        );
        transitionImageLayout(commandBuffer, textureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, mipLevels, 6);
        endSingleTimeCommands(device, commandPool, queue, commandBuffer);

        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);

        VkImageView imageView = createImageView(
            device,
            textureImage,
            imageFormat,
            1,
            VK_IMAGE_VIEW_TYPE_CUBE
        );

        m_images.push_back(textureImage);
        m_imageViews.push_back(imageView);

        return imageView;
    }

private:
    ktxVulkanDeviceInfo m_deviceInfo;
    std::vector<VkImage> m_images;
    std::vector<VkImageView> m_imageViews;
    std::vector<ktxVulkanTexture> m_ktxTextures;
};
