#pragma once

#include <array>
#include <vector>
#include "Vertex.h"

std::vector<Vertex> createSphereMesh(int subdivide = 0, float radius = 1.0f) {
    // Icosahedron
    static const float a = 0.525731112119;
    static const float b = 0.850650808352;
    static const std::array<glm::vec3, 12> vertices {{
        {0, a, b}, {0, a, -b}, {0, -a, b}, {0, -a, -b},  // X plane
        {b, 0, a}, {-b, 0, a}, {b, 0, -a}, {-b, 0, -a},  // Y plane
        {a, b, 0}, {a, -b, 0}, {-a, b, 0}, {-a, -b, 0},  // Z plane
    }};
    std::vector<glm::vec3> triangleVertices {
        vertices[0], vertices[8], vertices[10],
        vertices[1], vertices[10], vertices[8],
        vertices[0], vertices[5], vertices[2],
        vertices[0], vertices[2], vertices[4],
        vertices[1], vertices[6], vertices[3],
        vertices[1], vertices[3], vertices[7],
        vertices[2], vertices[11], vertices[9],
        vertices[3], vertices[9], vertices[11],
        vertices[5], vertices[10], vertices[7],
        vertices[5], vertices[7], vertices[11],
        vertices[4], vertices[6], vertices[8],
        vertices[4], vertices[9], vertices[6],
        vertices[0], vertices[10], vertices[5],
        vertices[0], vertices[4], vertices[8],
        vertices[1], vertices[7], vertices[10],
        vertices[1], vertices[8], vertices[6],
        vertices[2], vertices[5], vertices[11],
        vertices[2], vertices[9], vertices[4],
        vertices[3], vertices[11], vertices[7],
        vertices[3], vertices[6], vertices[9],
    };

    int finalVertexCount = 20 * std::pow(4, subdivide);
    triangleVertices.reserve(finalVertexCount);
    int triangleCount = 20;
    for (int subdivIteration = 0; subdivIteration < subdivide; ++subdivIteration) {
        for (int i = 0; i < triangleCount; ++i) {
            glm::vec3 v0 = triangleVertices[i * 3];
            glm::vec3 v1 = triangleVertices[i * 3 + 1];
            glm::vec3 v2 = triangleVertices[i * 3 + 2];
            glm::vec3 v3 = glm::normalize(v0 + v1);
            glm::vec3 v4 = glm::normalize(v1 + v2);
            glm::vec3 v5 = glm::normalize(v2 + v0);
            triangleVertices[i * 3] = v3;
            triangleVertices[i * 3 + 1] = v4;
            triangleVertices[i * 3 + 2] = v5;
            triangleVertices.push_back(v0);
            triangleVertices.push_back(v3);
            triangleVertices.push_back(v5);
            triangleVertices.push_back(v1);
            triangleVertices.push_back(v4);
            triangleVertices.push_back(v3);
            triangleVertices.push_back(v2);
            triangleVertices.push_back(v5);
            triangleVertices.push_back(v4);
        }

        // each subdivision gives x4 triangles: 80, 320, 1280
        triangleCount *= 4;
    }

    std::vector<Vertex> result;
    for (auto const& v : triangleVertices) {
        // set normals to vertex values which leads to smoothed normals
        result.push_back({.pos=v * radius, .normal=v});
    }

    return result;
}
