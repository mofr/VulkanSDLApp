#pragma once

#include <vector>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include "TextureLoader.h"

struct Envmap {
    VkImageView backgroundImageView;
    std::vector<glm::vec3> diffuseSphericalHarmonics;
};
