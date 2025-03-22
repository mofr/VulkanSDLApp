#pragma once

#include "Model.h"

struct MeshObject {
    uint32_t vertexCount;
    VkBuffer vertexBuffer;
    VkImage textureImage;
    VkImageView textureImageView;
    float modelAngleY = 0.0f;
    float modelScale = 1.0f;
    VkDescriptorSet material;

    glm::mat4 getTransform() {
        glm::mat4 transformMatrix = glm::mat4(1.0f);
        transformMatrix = glm::translate(transformMatrix, glm::vec3(0.0f, 0.0f, 0.0f));
        transformMatrix = glm::rotate(transformMatrix, glm::radians(modelAngleY), glm::vec3(0.0f, 1.0f, 0.0f));
        transformMatrix = glm::scale(transformMatrix, glm::vec3(modelScale));
        return transformMatrix;
    }
};

MeshObject transferModelToVulkan(VkPhysicalDevice physicalDevice, VkDevice device, VkCommandPool commandPool, VkQueue queue, VkSampler textureSampler, Pipeline& pipeline, const Model& model) {
    MeshObject object{};
    object.vertexBuffer = createVertexBuffer(physicalDevice, device, model.vertices);
    object.vertexCount = model.vertices.size();
    if (model.diffuseTexture.empty()) {
        ImageData whitePixel;
        uint8_t * whitePixelData = (uint8_t*) malloc(4);
        whitePixelData[0] = 255;
        whitePixelData[1] = 255;
        whitePixelData[2] = 255;
        whitePixelData[3] = 255;
        whitePixel.data.reset((void*) whitePixelData);
        whitePixel.imageFormat = VK_FORMAT_R8G8B8A8_UNORM;
        whitePixel.dataSize = 4;
        whitePixel.width = 1;
        whitePixel.height = 1;
        object.textureImage = createTextureImage(physicalDevice, device, commandPool, queue, whitePixel);
    }
    else {
        object.textureImage = createTextureImage(physicalDevice, device, commandPool, queue, "resources/" + model.diffuseTexture);
    }
    object.textureImageView = createImageView(device, object.textureImage, VK_FORMAT_R8G8B8A8_SRGB);
    Pipeline::MaterialProps materialProps{
        .diffuseColor = model.diffuseColor,
        .specularHardness = model.specularHardness,
        .specularPower = model.specularPower,
    };
    object.material = pipeline.createMaterial(object.textureImageView, textureSampler, materialProps);
    return object;
}
