#pragma once

#include <string>
#include <glm/glm.hpp>

struct Material {
    std::string baseColorTexture;
    std::string normalTexture;
    std::string roughnessTexture;
    glm::vec3 baseColorFactor {1.0f};
    glm::vec3 emitFactor {0.0f};
    float roughnessFactor = 1.0f;
    float metallicFactor = 0.0f;
};
