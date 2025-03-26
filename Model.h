#pragma once

#include <vector>
#include <string>
#include "Vertex.h"
#include "Material.h"

struct Model {
    std::vector<Vertex> vertices;
    Material material;
};
