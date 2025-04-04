#pragma once

#include "Model.h"

struct MeshObject {
    uint32_t vertexCount;
    VkBuffer vertexBuffer;

    VkImage textureImage;
    VkImageView textureImageView;
    VkSampler textureSampler;
    uint32_t mipLevels;

    Material material;
    VkDescriptorSet materialDescriptorSet;

    glm::vec3 position;
    float angleY = 0.0f;
    float scale = 1.0f;

    glm::mat4 getTransform() const {
        glm::mat4 transformMatrix = glm::mat4(1.0f);
        transformMatrix = glm::translate(transformMatrix, position);
        transformMatrix = glm::rotate(transformMatrix, glm::radians(angleY), glm::vec3(0.0f, 1.0f, 0.0f));
        transformMatrix = glm::scale(transformMatrix, glm::vec3(scale));
        return transformMatrix;
    }
};
