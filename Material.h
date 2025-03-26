#pragma once

#include <string>
#include <glm/glm.hpp>

struct Material {
    std::string diffuseTexture;
    std::string normalTexture;
    glm::vec3 diffuseFactor{1.0f};
    glm::vec3 emitFactor{0.0f};
    float specularHardness = 1.0f;
    float specularPower = 1.0f;
};
