#pragma once

#include <vector>
#include <string>
#include "Vertex.h"

struct Model {
    std::vector<Vertex> vertices;
    std::string diffuseTexture;
    glm::vec3 diffuseColor{1.0f, 1.0f, 1.0f};
    float specularHardness;
    float specularPower;
};
