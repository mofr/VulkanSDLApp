#pragma once

#include <vector>
#include <string>
#include "Vertex.h"

struct Model {
    std::vector<Vertex> vertices;
    std::string diffuseTexture;
    float specularHardness;
    float specularPower;
};
