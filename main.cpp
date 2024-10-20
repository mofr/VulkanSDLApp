#include <iostream>
#include <fstream>
#include <map>
#include <chrono>
#include <SDL.h>
#include <SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_metal.h>

#define GLM_FORCE_LEFT_HANDED
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/ext.hpp>

#include "tiny_obj_loader.h"
#include "stb_image.h"

#include "Pipeline.h"
#include "Vertex.h"

struct ModelTransform {
    glm::mat4 model;
};

struct ViewProjection {
    glm::mat4 view;
    glm::mat4 projection;
};

void normalizeModel(std::vector<Vertex>& vertices, float size = 1) {
    struct {glm::vec3 min; glm::vec3 max;} aabb{{999.0f, 999.0f, 999.0f}, {-999, -999, -999}};

    for (const auto& v : vertices) {
        aabb.min = glm::min(aabb.min, v.pos);
        aabb.max = glm::max(aabb.max, v.pos);
    }

    glm::vec3 center = (aabb.max + aabb.min) / 2.0f;
    float scale = glm::min(
        size / (aabb.max.x - aabb.min.x),
        size / (aabb.max.y - aabb.min.y)
    );
    for (auto& v : vertices) {
        v.pos -= center;
        v.pos *= scale;
    }
}

uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        // Check if the memory type is supported and if it matches the requested properties
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i; // Found a suitable memory type
        }
    }

    throw std::runtime_error("failed to find suitable memory type!");
}

VkBuffer createBuffer(VkDevice device, VkPhysicalDevice physicalDevice, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkDeviceMemory *bufferMemory) {
    VkBuffer buffer;
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(device, &allocInfo, nullptr, bufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate buffer memory!");
    }

    vkBindBufferMemory(device, buffer, *bufferMemory, 0);
    return buffer;
}

bool checkValidationLayerSupport(const std::vector<const char*>& validationLayers) {
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char* layerName : validationLayers) {
        bool layerFound = false;

        for (const auto& layerProperties : availableLayers) {
            if (strcmp(layerName, layerProperties.layerName) == 0) {
                layerFound = true;
                break;
            }
        }

        if (!layerFound) {
            return false; // Layer not found
        }
    }

    return true; // All layers found
}

VkFormat findSupportedFormat(VkPhysicalDevice physicalDevice, const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) {
    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
            return format;
        } else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }

    throw std::runtime_error("failed to find supported format!");
}

VkFormat findDepthFormat(VkPhysicalDevice physicalDevice) {
    return findSupportedFormat(
        physicalDevice,
        {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
    );
}

bool hasStencilComponent(VkFormat format) {
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

void createImage(VkPhysicalDevice physicalDevice, VkDevice device, uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
        throw std::runtime_error("failed to create image!");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate image memory!");
    }

    vkBindImageMemory(device, image, imageMemory, 0);
}

std::vector<Vertex> loadObj(const std::string& filePath) {
    tinyobj::ObjReaderConfig reader_config;
    reader_config.mtl_search_path = "";

    tinyobj::ObjReader reader;

    if (!reader.ParseFromFile(filePath, reader_config)) {
        if (!reader.Error().empty()) {
            std::cerr << "TinyObjReader: " << reader.Error();
        }
        throw std::runtime_error("failed to parse obj file!");
    }

    if (!reader.Warning().empty()) {
        std::cout << "TinyObjReader: " << reader.Warning();
    }

    auto& attrib = reader.GetAttrib();
    auto& shapes = reader.GetShapes();
    auto& materials = reader.GetMaterials();

    std::vector<Vertex> vertices;
    for (size_t s = 0; s < shapes.size(); s++) {
        size_t index_offset = 0;
        for (size_t f = 0; f < shapes[s].mesh.num_face_vertices.size(); f++) {
            size_t fv = size_t(shapes[s].mesh.num_face_vertices[f]);

            size_t material_index = shapes[s].mesh.material_ids[f];
            glm::vec3 color = glm::vec3(1.0f);
            if (material_index < materials.size()) {
                auto diffuse = materials[material_index].diffuse;
                color.r = diffuse[0];
                color.g = diffuse[1];
                color.b = diffuse[2];
            }

            bool has_normals = false;

            // Loop over vertices in the face.
            for (size_t v = 0; v < fv; v++) {
                // access to vertex
                tinyobj::index_t idx = shapes[s].mesh.indices[index_offset + v];
                tinyobj::real_t vx = attrib.vertices[3*size_t(idx.vertex_index)+0];
                tinyobj::real_t vy = attrib.vertices[3*size_t(idx.vertex_index)+1];
                tinyobj::real_t vz = attrib.vertices[3*size_t(idx.vertex_index)+2];

                glm::vec3 normal{};

                // Check if `normal_index` is zero or positive. negative = no normal data
                if (idx.normal_index >= 0) {
                    tinyobj::real_t nx = attrib.normals[3*size_t(idx.normal_index)+0];
                    tinyobj::real_t ny = attrib.normals[3*size_t(idx.normal_index)+1];
                    tinyobj::real_t nz = attrib.normals[3*size_t(idx.normal_index)+2];
                    normal = {nx, ny, nz};
                    has_normals = true;
                }

                glm::vec2 uv{};

                // Check if `texcoord_index` is zero or positive. negative = no texcoord data
                if (idx.texcoord_index >= 0) {
                    tinyobj::real_t tx = attrib.texcoords[2*size_t(idx.texcoord_index)+0];
                    tinyobj::real_t ty = attrib.texcoords[2*size_t(idx.texcoord_index)+1];
                    uv = {tx, 1 - ty};
                }

                // Optional: vertex colors
                // tinyobj::real_t red   = attrib.colors[3*size_t(idx.vertex_index)+0];
                // tinyobj::real_t green = attrib.colors[3*size_t(idx.vertex_index)+1];
                // tinyobj::real_t blue  = attrib.colors[3*size_t(idx.vertex_index)+2];

                vertices.push_back({{vx, vy, vz}, normal, color, uv});
            }

            if (!has_normals) {
                glm::vec3 v0 = vertices[index_offset].pos;
                glm::vec3 v1 = vertices[index_offset + 1].pos;
                glm::vec3 v2 = vertices[index_offset + 2].pos;

                // Calculate the two edges of the triangle
                glm::vec3 edge1 = v1 - v0;
                glm::vec3 edge2 = v2 - v0;

                // Calculate the normal using the cross product
                glm::vec3 normal = glm::normalize(glm::cross(edge1, edge2));
                vertices[index_offset].normal = normal;
                vertices[index_offset + 1].normal = normal;
                vertices[index_offset + 2].normal = normal;
            }

            index_offset += fv;
        }
    }

    // In .obj forward direction is -Z. In my code forward direction is Z.
    // Need to inverse Z for all vertices and normals. And ensure that the vertex order remain clockwise.
    // In Vulkan Y goes down. In Obj - up.
    for (auto& v : vertices) {
        v.pos.z *= -1;
        v.pos.y *= -1;
        v.normal.z *= -1;
        v.normal.y *= -1;
    }
    for (size_t i = 0; i < vertices.size() - 2; i += 3){
        glm::vec3& v0 = vertices[i].pos;
        glm::vec3& v1 = vertices[i + 1].pos;
        glm::vec3& v2 = vertices[i + 2].pos;

        glm::vec3 intendedNormal = vertices[0].normal;
        glm::vec3 computedNormal = glm::normalize(glm::cross(v1 - v0, v2 - v0));

        if (computedNormal != intendedNormal) {
            std::swap(v1, v2);
        }
    }

    return vertices;
}

glm::mat4 cameraLookAt(glm::vec3 const& eye, glm::vec3 const& center, glm::vec3 const& up)
{
    glm::vec3 f(normalize(center - eye));
    glm::vec3 r(normalize(cross(f, up)));
    glm::vec3 u(cross(f, r));

    glm::mat4 result(1);
    result[0][0] = r.x;
    result[1][0] = r.y;
    result[2][0] = r.z;
    result[0][1] = u.x;
    result[1][1] = u.y;
    result[2][1] = u.z;
    result[0][2] = f.x;
    result[1][2] = f.y;
    result[2][2] = f.z;
    result[3][0] = -dot(r, eye);
    result[3][1] = -dot(u, eye);
    result[3][2] = -dot(f, eye);
    return result;
}

VkCommandBuffer beginSingleTimeCommands(VkDevice device, VkCommandPool commandPool) {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    return commandBuffer;
}

void endSingleTimeCommands(VkDevice device, VkCommandPool commandPool, VkQueue queue, VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

void copyBuffer(VkDevice device, VkCommandPool commandPool, VkQueue queue, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
    VkCommandBuffer commandBuffer = beginSingleTimeCommands(device, commandPool);

    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

    endSingleTimeCommands(device, commandPool, queue, commandBuffer);
}

void transitionImageLayout(VkDevice device, VkCommandPool commandPool, VkQueue queue, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkCommandBuffer commandBuffer = beginSingleTimeCommands(device, commandPool);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        throw std::invalid_argument("unsupported layout transition!");
    }

    vkCmdPipelineBarrier(
        commandBuffer,
        sourceStage,
        destinationStage,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier
    );

    endSingleTimeCommands(device, commandPool, queue, commandBuffer);
}

void copyBufferToImage(VkDevice device, VkCommandPool commandPool, VkQueue queue, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
    VkCommandBuffer commandBuffer = beginSingleTimeCommands(device, commandPool);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;

    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;

    region.imageOffset = {0, 0, 0};
    region.imageExtent = {
        width,
        height,
        1
    };

    vkCmdCopyBufferToImage(
        commandBuffer,
        buffer,
        image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region
    );

    endSingleTimeCommands(device, commandPool, queue, commandBuffer);
}

VkImage createTextureImage(VkPhysicalDevice physicalDevice, VkDevice device, VkCommandPool commandPool, VkQueue queue, const char * filename) {
    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load(filename, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

    if (!pixels) {
        throw std::runtime_error("failed to load texture image!");
    }

    VkDeviceMemory stagingBufferMemory;
    VkDeviceSize imageSize = texWidth * texHeight * 4;
    VkBuffer stagingBuffer = createBuffer(device, physicalDevice, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingBufferMemory);
    {
        void* data;
        vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data);
        memcpy(data, pixels, static_cast<size_t>(imageSize));
        vkUnmapMemory(device, stagingBufferMemory);
    }
    stbi_image_free(pixels);

    VkImage textureImage;
    VkDeviceMemory textureImageMemory;

    createImage(
        physicalDevice,
        device,
        texWidth,
        texHeight,
        VK_FORMAT_R8G8B8A8_SRGB,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        textureImage,
        textureImageMemory
    );

    transitionImageLayout(device, commandPool, queue, textureImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copyBufferToImage(device, commandPool, queue, stagingBuffer, textureImage, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));
    transitionImageLayout(device, commandPool, queue, textureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);

    return textureImage;
}

VkImageView createImageView(VkDevice device, VkImage image, VkFormat format) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView;
    if (vkCreateImageView(device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
        throw std::runtime_error("failed to create texture image view!");
    }

    return imageView;
}

VkSampler createTextureSampler(VkDevice device, VkPhysicalDevice physicalDevice) {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    VkSampler textureSampler;
    if (vkCreateSampler(device, &samplerInfo, nullptr, &textureSampler) != VK_SUCCESS) {
        throw std::runtime_error("failed to create texture sampler!");
    }
    return textureSampler;
}

VkBuffer createVertexBuffer(VkPhysicalDevice physicalDevice, VkDevice device, std::vector<Vertex> const& vertices) {
    VkBuffer vertexBuffer;
    VkDeviceMemory vertexBufferMemory;
    VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();
    vertexBuffer = createBuffer(device, physicalDevice, bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &vertexBufferMemory);
    {
        void* data = nullptr;
        vkMapMemory(device, vertexBufferMemory, 0, bufferSize, 0, &data);
        memcpy(data, vertices.data(), (size_t)bufferSize);
        vkUnmapMemory(device, vertexBufferMemory);
    }
    return vertexBuffer;
}

void createUniformBuffer(VkPhysicalDevice physicalDevice, VkDevice device, VkBuffer& buffer, VkDeviceMemory& bufferMemory, VkDeviceSize bufferSize) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create uniform buffer!");
    }
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memRequirements);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate uniform buffer memory!");
    }
    vkBindBufferMemory(device, buffer, bufferMemory, 0);
}

int main() {
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

    std::vector extensions = {
        VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
        VK_EXT_METAL_SURFACE_EXTENSION_NAME,
        VK_KHR_SURFACE_EXTENSION_NAME,
        "VK_KHR_get_physical_device_properties2",
    };

    const std::vector validationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };
    if (!checkValidationLayerSupport(validationLayers)) {
        throw std::runtime_error("Validation layers requested, but not available!");
    }

    VkInstance instance;
    {
        VkInstanceCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR,
            .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
            .ppEnabledExtensionNames = extensions.data(),
            .enabledLayerCount = static_cast<uint32_t>(validationLayers.size()),
            .ppEnabledLayerNames = validationLayers.data(),
        };

        VkResult result = vkCreateInstance(&createInfo, nullptr, &instance);
        if (result != VK_SUCCESS) {
            std::cerr << "Failed to create Vulkan instance! " << result << std::endl;
            SDL_DestroyWindow(window);
            SDL_Quit();
            return -1;
        }
    }

    VkSurfaceKHR surface;
    if (SDL_Vulkan_CreateSurface(window, instance, &surface) != SDL_TRUE) {
        std::cerr << "Failed to create Vulkan surface: " << SDL_GetError() << std::endl;
        return -1;
    }

    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    std::cout << "Found " << deviceCount << " Physical devices" << std::endl;
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    for (const auto& device : devices) {
        VkPhysicalDeviceFeatures supportedFeatures;
        supportedFeatures.samplerAnisotropy = VK_TRUE;
        vkGetPhysicalDeviceFeatures(device, &supportedFeatures);
        if (!supportedFeatures.samplerAnisotropy) {
            std::cout << "Sampler anisotropy is not supported!" << std::endl;
        }
        physicalDevice = device;
        break;
    }

    if (physicalDevice == VK_NULL_HANDLE) {
        std::cerr << "Failed to create physical device!" << std::endl;
        return -1;
    }

    VkDevice device;
    uint32_t graphicsQueueFamilyIndex = UINT32_MAX;
    {
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

        for (size_t i = 0; i < queueFamilies.size(); i++) {
            const auto& queueFamily = queueFamilies[i];

            if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                graphicsQueueFamilyIndex = i;
                break;
            }
        }

        if (graphicsQueueFamilyIndex == UINT32_MAX) {
            std::cerr << "Failed to find a graphics queue family!" << std::endl;
            return -1;
        }

        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = graphicsQueueFamilyIndex;
        queueCreateInfo.queueCount = 1;

        float queuePriority = 1.0f;
        queueCreateInfo.pQueuePriorities = &queuePriority;

        std::vector deviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            "VK_KHR_portability_subset",
        };
        VkPhysicalDeviceFeatures deviceFeatures{};
        deviceFeatures.samplerAnisotropy = VK_TRUE;
        VkDeviceCreateInfo deviceCreateInfo{};
        deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceCreateInfo.queueCreateInfoCount = 1;
        deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
        deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
        deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();
        deviceCreateInfo.pEnabledFeatures = &deviceFeatures;
        VkResult result = vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device);
        if (result != VK_SUCCESS) {
            std::cerr << "Failed to create Vulkan device! " << result << std::endl;
            SDL_DestroyWindow(window);
            SDL_Quit();
            return -1;
        }
    }

    VkQueue graphicsQueue;
    vkGetDeviceQueue(device, graphicsQueueFamilyIndex, 0, &graphicsQueue);
    if (graphicsQueue == VK_NULL_HANDLE) {
        std::cerr << "Failed to get graphics queue!" << std::endl;
        return -1;
    }

    VkQueue presentQueue;
    vkGetDeviceQueue(device, graphicsQueueFamilyIndex, 0, &presentQueue);
    if (presentQueue == VK_NULL_HANDLE) {
        std::cerr << "Failed to get present queue!" << std::endl;
        return -1;
    }

    // Get supported formats
    uint32_t formatCount;
    {
        VkResult result = vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
        if (result != VK_SUCCESS) {
            std::cerr << "Failed to get surface formats!" << std::endl;
            return -1;
        }
    }
    std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, surfaceFormats.data());
    VkSurfaceFormatKHR selectedFormat;
    {
        bool found = false;
        for (const VkSurfaceFormatKHR& format : surfaceFormats) {
            if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                selectedFormat = format;
                found = true;
                break;
            }
        }
        if (!found) {
            throw std::runtime_error("sRGB is not supported");
        }
    }

    // Get supported presentation modes
    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, presentModes.data());
    VkPresentModeKHR selectedPresentMode = VK_PRESENT_MODE_FIFO_KHR; // Vsync mode, can also be VK_PRESENT_MODE_IMMEDIATE_KHR

    VkSurfaceCapabilitiesKHR surfaceCapabilities;
    {
        VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCapabilities);
        if (result != VK_SUCCESS) {
            std::cerr << "Failed to get surface capabilities!" << std::endl;
            return -1;
        }
    }
    VkExtent2D swapchainExtent;
    if (surfaceCapabilities.currentExtent.width != UINT32_MAX) {
        swapchainExtent = surfaceCapabilities.currentExtent;
    } else {
        // Handle dynamic extent (if supported)
        swapchainExtent = { width, height }; // Replace with your window dimensions
    }

    uint32_t maxFramesInFlight = surfaceCapabilities.minImageCount + 1;
    if (surfaceCapabilities.maxImageCount > 0 && maxFramesInFlight > surfaceCapabilities.maxImageCount) {
        maxFramesInFlight = surfaceCapabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR swapchainInfo{};
    swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainInfo.surface = surface;
    swapchainInfo.minImageCount = maxFramesInFlight;
    swapchainInfo.imageFormat = selectedFormat.format;
    swapchainInfo.imageColorSpace = selectedFormat.colorSpace;
    swapchainInfo.imageExtent = swapchainExtent;
    swapchainInfo.imageArrayLayers = 1;
    swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainInfo.queueFamilyIndexCount = 0;
    swapchainInfo.preTransform = surfaceCapabilities.currentTransform;
    swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; // Adjust for windowed mode if needed
    swapchainInfo.presentMode = selectedPresentMode;
    swapchainInfo.clipped = VK_TRUE;
    swapchainInfo.oldSwapchain = VK_NULL_HANDLE; // For the initial swapchain

    VkSwapchainKHR swapchain;
    if (vkCreateSwapchainKHR(device, &swapchainInfo, nullptr, &swapchain) != VK_SUCCESS) {
        std::cerr << "Failed to create swapchain: " << std::endl;
        return -1;
    }

    std::vector<VkImage> swapchainImages(maxFramesInFlight);
    vkGetSwapchainImagesKHR(device, swapchain, &maxFramesInFlight, swapchainImages.data());

    std::vector<VkImageView> swapchainImageViews(swapchainImages.size());
    for (size_t i = 0; i < swapchainImages.size(); i++) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = swapchainImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = selectedFormat.format; // The format selected earlier
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &viewInfo, nullptr, &swapchainImageViews[i]) != VK_SUCCESS) {
            std::cerr << "Failed to create image views!" << std::endl;
            return -1;
        }
    }

    VkCommandPool commandPool;
    {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = graphicsQueueFamilyIndex; // Use the graphics queue family
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; // Enable reset command buffer
        if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
            std::cerr << "Failed to create command pool!" << std::endl;
            return -1;
        }
    }

    VkImage depthImage;
    VkDeviceMemory depthImageMemory;
    VkImageView depthImageView;
    VkFormat depthFormat = findDepthFormat(physicalDevice);
    {
        createImage(
            physicalDevice,
            device,
            swapchainExtent.width,
            swapchainExtent.height,
            depthFormat,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            depthImage,
            depthImageMemory
        );

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = depthImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = depthFormat;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &viewInfo, nullptr, &depthImageView) != VK_SUCCESS) {
            std::cerr << "Failed to create image views!" << std::endl;
            return -1;
        }
    }

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = selectedFormat.format; // The format used for swapchain images
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT; // Sample count
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; // Clear before use
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // Store after rendering
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // Ignore stencil
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; // Initial layout
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; // Final layout
    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0; // Reference the first attachment
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; // Layout for color attachment

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = depthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkRenderPassCreateInfo renderPassCreateInfo{};
    renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    std::array attachments = {colorAttachment, depthAttachment};
    renderPassCreateInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassCreateInfo.pAttachments = attachments.data();
    renderPassCreateInfo.subpassCount = 1;
    renderPassCreateInfo.pSubpasses = &subpass;

    VkRenderPass renderPass;
    if (vkCreateRenderPass(device, &renderPassCreateInfo, nullptr, &renderPass) != VK_SUCCESS) {
        std::cerr << "Failed to create render pass!" << std::endl;
        return -1;
    }

    std::vector<VkFramebuffer> framebuffers(swapchainImageViews.size());
    for (size_t i = 0; i < swapchainImageViews.size(); i++) {
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        std::array attachments = {
            swapchainImageViews[i],
            depthImageView,
        };
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = swapchainExtent.width;
        framebufferInfo.height = swapchainExtent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &framebuffers[i]) != VK_SUCCESS) {
            std::cerr << "Failed to create framebuffer!" << std::endl;
            return -1;
        }
    }

    std::vector<VkCommandBuffer> commandBuffers(swapchainImageViews.size());
    {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; // Primary command buffers can be submitted to queues
        allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());
        if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
            std::cerr << "Failed to allocate command buffers!" << std::endl;
            return -1;
        }
    }

    Pipeline pipeline(device, swapchainExtent, renderPass, 1024);

    VkBuffer viewProjectionBuffer;
    VkDeviceMemory viewProjectionBufferMemory;
    createUniformBuffer(physicalDevice, device, viewProjectionBuffer, viewProjectionBufferMemory, sizeof(ViewProjection));
    VkDescriptorSet viewProjectionDescriptorSet = pipeline.createDescriptorSet1();
    {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = viewProjectionBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(ViewProjection);

        std::array writes = {
            VkWriteDescriptorSet{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = viewProjectionDescriptorSet,
                .dstBinding = 0,
                .dstArrayElement = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .pBufferInfo = &bufferInfo,
            },
        };
        vkUpdateDescriptorSets(device, writes.size(), writes.data(), 0, nullptr);
    }

    struct Mesh {
        std::vector<Vertex> vertices;
        VkBuffer vertexBuffer;
        VkImage textureImage;
        VkImageView textureImageView;
        VkSampler textureSampler;
    };

    struct Object {
        Mesh mesh; // shared between objects
        VkDescriptorSet descriptorSet; // supposed to have only model transform
    };

    Object tower{};
    Mesh towerMesh{};
    towerMesh.vertices = loadObj("10_15_2024.obj");
    normalizeModel(towerMesh.vertices, 3);
    towerMesh.vertexBuffer = createVertexBuffer(physicalDevice, device, towerMesh.vertices);
    towerMesh.textureImage = createTextureImage(physicalDevice, device, commandPool, graphicsQueue, "textures/material_color.jpeg");
    towerMesh.textureImageView = createImageView(device, towerMesh.textureImage, VK_FORMAT_R8G8B8A8_SRGB);
    towerMesh.textureSampler = createTextureSampler(device, physicalDevice);
    tower.mesh = towerMesh;
    tower.descriptorSet = pipeline.createDescriptorSet0();
    VkBuffer modelTransformBuffer;
    VkDeviceMemory modelTransformBufferMemory;
    createUniformBuffer(physicalDevice, device, modelTransformBuffer, modelTransformBufferMemory, sizeof(ModelTransform));
    {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = modelTransformBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(ModelTransform);

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = towerMesh.textureImageView;
        imageInfo.sampler = towerMesh.textureSampler;

        std::array writes = {
            VkWriteDescriptorSet{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = tower.descriptorSet,
                .dstBinding = 0,
                .dstArrayElement = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .pBufferInfo = &bufferInfo,
            },
            VkWriteDescriptorSet{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = tower.descriptorSet,
                .dstBinding = 1,
                .dstArrayElement = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .pImageInfo = &imageInfo,
            },
        };
        vkUpdateDescriptorSets(device, writes.size(), writes.data(), 0, nullptr);
    }

    auto body = loadObj("Model_D0702033/Body_Posed.obj");
    auto gizmo = loadObj("gizmo.obj");
    auto monkey = loadObj("monkey.obj");
    auto pixar = loadObj("PIXAR light.obj");
    auto horse = loadObj("horse.obj");

    // std::vector<Vertex> vertices = {};
    // vertices.insert(vertices.end(), body.begin(), body.end());
    // vertices.insert(vertices.end(), gizmo.begin(), gizmo.end());
    // vertices.insert(vertices.end(), monkey.begin(), monkey.end());
    // vertices.insert(vertices.end(), tower.begin(), tower.end());
    // vertices.insert(vertices.end(), pixar.begin(), pixar.end());
    // vertices.insert(vertices.end(), horse.begin(), horse.end());

    Object obj2{};
    { // rectangle with texture
        Mesh mesh = {};
        mesh.vertices.push_back({{-1.0f, 0, 1.0f}, {0, -1.0f, 0}, {1.0f, 1.0f, 1.0f}, {0, 0}});
        mesh.vertices.push_back({{1.0f, 0, 1.0f}, {0, -1.0f, 0}, {1.0f, 1.0f, 1.0f}, {1, 0}});
        mesh.vertices.push_back({{1.0f, 0, -1.0f}, {0, -1.0f, 0}, {1.0f, 1.0f, 1.0f}, {1, 1}});

        mesh.vertices.push_back({{1.0f, 0, -1.0f}, {0, -1.0f, 0}, {1.0f, 1.0f, 1.0f}, {1, 1}});
        mesh.vertices.push_back({{-1.0f, 0, -1.0f}, {0, -1.0f, 0}, {1.0f, 1.0f, 1.0f}, {0, 1}});
        mesh.vertices.push_back({{-1.0f, 0, 1.0f}, {0, -1.0f, 0}, {1.0f, 1.0f, 1.0f}, {0, 0}});

        mesh.vertexBuffer = createVertexBuffer(physicalDevice, device, mesh.vertices);
        mesh.textureImage = createTextureImage(physicalDevice, device, commandPool, graphicsQueue, "textures/texture.jpg");
        mesh.textureImageView = createImageView(device, mesh.textureImage, VK_FORMAT_R8G8B8A8_SRGB);
        mesh.textureSampler = createTextureSampler(device, physicalDevice);
        obj2.mesh = mesh;
    }
    obj2.descriptorSet = pipeline.createDescriptorSet0();


    // { // vulkan space, where Z and goes away, X goes right and Y goes down
    //     // X axis plane
    //     vertices.push_back({{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}});
    //     vertices.push_back({{0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}});
    //     vertices.push_back({{0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}});
    //
    //     // Y axis plane
    //     vertices.push_back({{0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}});
    //     vertices.push_back({{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}});
    //     vertices.push_back({{0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}});
    //
    //     // Z axis plane
    //     vertices.push_back({{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}});
    //     vertices.push_back({{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}});
    //     vertices.push_back({{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}});
    // }

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    std::vector<VkSemaphore> imageAvailableSemaphores(maxFramesInFlight);
    std::vector<VkSemaphore> renderFinishedSemaphores(maxFramesInFlight);
    std::vector<VkFence> renderFences(maxFramesInFlight);
    for (size_t i = 0; i < maxFramesInFlight; ++i) {
        vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]);
        vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]);
        vkCreateFence(device, &fenceInfo, nullptr, &renderFences[i]);
    }

    glm::mat4 projection = glm::perspective(glm::radians(45.0f), float(width) / float(height), 0.1f, 100.0f);
    glm::mat4 view = cameraLookAt(glm::vec3(0.0f, -3.0f, -5.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f));
    glm::mat4 model = glm::mat4(1.0f);
    float modelAngleY{};
    float modelScale = 1.0f;

    typedef std::chrono::steady_clock Clock;
    auto lastUpdateTime = Clock::now();
    size_t currentFrame = 0;
    bool running = true;
    SDL_Event event;
    while (running) {
        vkWaitForFences(device, 1, &renderFences[currentFrame], true, UINT64_MAX);

        // Acquire an image from the swapchain
        uint32_t imageIndex;
        {
            VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);
            if (result == VK_ERROR_OUT_OF_DATE_KHR) {
                // recreateSwapChain();
                std::cerr << "swapchain is out of date and needs to be recreated" << std::endl;
                return 0;
            } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
                throw std::runtime_error("failed to acquire swap chain image!");
            }
        }

        vkResetFences(device, 1, &renderFences[currentFrame]);

        model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(0.0f, 0.0f, 0.0f));
        model = glm::rotate(model, glm::radians(modelAngleY), glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::scale(model, glm::vec3(modelScale));

        // frame-level data
        {
            ViewProjection vp{};
            vp.view = view;
            vp.projection = projection;
            void* data;
            vkMapMemory(device, viewProjectionBufferMemory, 0, sizeof(vp), 0, &data);
            memcpy(data, &vp, sizeof(vp));
            vkUnmapMemory(device, viewProjectionBufferMemory);
        }

        // object-level data
        {
            ModelTransform modelTransform{};
            modelTransform.model = model;
            void* data;
            vkMapMemory(device, modelTransformBufferMemory, 0, sizeof(modelTransform), 0, &data);
            memcpy(data, &modelTransform, sizeof(modelTransform));
            vkUnmapMemory(device, modelTransformBufferMemory);
        }

        // record commands
        VkCommandBuffer commandBuffer = commandBuffers[currentFrame];
        vkResetCommandBuffer(commandBuffer, 0);
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
            std::cerr << "Failed to begin recording command buffer!" << std::endl;
            return -1;
        }

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = renderPass;
        renderPassInfo.framebuffer = framebuffers[currentFrame];
        renderPassInfo.renderArea.offset = { 0, 0 };
        renderPassInfo.renderArea.extent = { swapchainExtent.width, swapchainExtent.height };

        std::array clearValues{
            VkClearValue{.color={{0.05f, 0.05f, 0.05f, 1.0f}}},
            VkClearValue{.depthStencil={1.0f, 0}}
        };
        renderPassInfo.clearValueCount = clearValues.size();
        renderPassInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline);

        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout, 1, 1, &viewProjectionDescriptorSet, 0, nullptr);

        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout, 0, 1, &tower.descriptorSet, 0, nullptr);
        VkBuffer vertexBuffers[] = { tower.mesh.vertexBuffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        vkCmdDraw(commandBuffer, static_cast<uint32_t>(tower.mesh.vertices.size()), 1, 0, 0);

        vkCmdEndRenderPass(commandBuffer);

        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
            std::cerr << "Failed to record command buffer!" << std::endl;
            return -1;
        }

        // Submit the command buffer
        VkSubmitInfo submitInfo{};
        VkPipelineStageFlags waitStages[] = {
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT // Wait for color output stage
        };
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &imageAvailableSemaphores[currentFrame]; // Wait for the image to be available
        submitInfo.pWaitDstStageMask = waitStages; // Wait for color output stage
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffers[imageIndex]; // Command buffer for this image
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &renderFinishedSemaphores[currentFrame]; // Signal when rendering is finished
        if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, renderFences[currentFrame]) != VK_SUCCESS) {
            std::cerr << "Failed to submit draw command buffer!" << std::endl;
            return -1;
        }

        // Present the rendered image
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &renderFinishedSemaphores[currentFrame]; // Wait for rendering to finish
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapchain;
        presentInfo.pImageIndices = &imageIndex;

        if (vkQueuePresentKHR(presentQueue, &presentInfo) != VK_SUCCESS) {
            std::cerr << "Failed to present swapchain image!" << std::endl;
        }

        static const float maxFrameTime = 1.0f / 30.0f;
        auto now = Clock::now();
        float dt = std::chrono::duration<float>(now - lastUpdateTime).count();
        dt = glm::min(dt, maxFrameTime);
        lastUpdateTime = now;
        auto fps = static_cast<int>(1.0f / dt);
        std::string windowTitle = std::to_string(dt * 1000) + " ms, " + std::to_string(fps) + " FPS";
        SDL_SetWindowTitle(window, windowTitle.c_str());

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }
            if (event.type == SDL_MOUSEMOTION) {
                float normalizedX = static_cast<float>(event.motion.x) / width - 0.5f;
                modelAngleY = -normalizedX * 360.0f * 4;

                float normalizedY = static_cast<float>(event.motion.y) / height;
                modelScale = normalizedY * 5;
            }
            if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    running = false;
                }
            }
        }

        currentFrame = (currentFrame + 1) % maxFramesInFlight;
    }

    // Cleanup
    // vkDestroyInstance(instance, nullptr);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
