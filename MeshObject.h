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
    object.textureImage = createTextureImage(physicalDevice, device, commandPool, queue, model.diffuseTexture.c_str());
    object.textureImageView = createImageView(device, object.textureImage, VK_FORMAT_R8G8B8A8_SRGB);
    object.material = pipeline.createMaterial(object.textureImageView, textureSampler, model.specularHardness, model.specularPower);
    return object;
}
