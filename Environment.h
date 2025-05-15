#pragma once

#include <vector>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include "TextureLoader.h"

struct Environment {
    struct Sun {
        glm::vec3 dir;
        glm::vec3 radiance;
        float solidAngle;
    };
    VkImageView backgroundImageView;
    std::vector<glm::vec3> diffuseSphericalHarmonics;
    Sun sun = {};
};
