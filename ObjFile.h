#pragma once

#include <iostream>
#include <tiny_obj_loader.h>
#include "Vertex.h"
#include "Model.h"

void normalizeModel(std::vector<Vertex>& vertices, float size = 1) {
    struct {glm::vec3 min; glm::vec3 max;} aabb{{999.0f, 999.0f, 999.0f}, {-999, -999, -999}};

    for (const auto& v : vertices) {
        aabb.min = glm::min(aabb.min, v.pos);
        aabb.max = glm::max(aabb.max, v.pos);
    }

    glm::vec3 center = (aabb.max + aabb.min) / 2.0f;
    float scale = glm::min(
        size / (aabb.max.x - aabb.min.x),
        size / (aabb.max.y - aabb.min.y)
    );
    for (auto& v : vertices) {
        v.pos -= center;
        v.pos *= scale;
    }
}

Model loadObj(const std::string& filePath) {
    tinyobj::ObjReaderConfig reader_config;
    reader_config.mtl_search_path = "";

    tinyobj::ObjReader reader;

    if (!reader.ParseFromFile(filePath, reader_config)) {
        if (!reader.Error().empty()) {
            std::cerr << "TinyObjReader: " << reader.Error();
        }
        throw std::runtime_error("failed to parse obj file!");
    }

    if (!reader.Warning().empty()) {
        std::cout << "TinyObjReader: " << reader.Warning();
    }

    auto& attrib = reader.GetAttrib();
    auto& shapes = reader.GetShapes();
    auto& materials = reader.GetMaterials();

    std::string diffuseTexture;
    if (materials.size() > 0) {
        diffuseTexture = materials[0].diffuse_texname;
    }

    std::vector<Vertex> vertices;
    for (size_t s = 0; s < shapes.size(); s++) {
        size_t index_offset = 0;
        for (size_t f = 0; f < shapes[s].mesh.num_face_vertices.size(); f++) {
            size_t fv = size_t(shapes[s].mesh.num_face_vertices[f]);

            size_t material_index = shapes[s].mesh.material_ids[f];
            glm::vec3 color = glm::vec3(1.0f);
            if (material_index < materials.size()) {
                auto diffuse = materials[material_index].diffuse;
                color.r = diffuse[0];
                color.g = diffuse[1];
                color.b = diffuse[2];
            }

            bool has_normals = false;

            // Loop over vertices in the face.
            for (size_t v = 0; v < fv; v++) {
                // access to vertex
                tinyobj::index_t idx = shapes[s].mesh.indices[index_offset + v];
                tinyobj::real_t vx = attrib.vertices[3*size_t(idx.vertex_index)+0];
                tinyobj::real_t vy = attrib.vertices[3*size_t(idx.vertex_index)+1];
                tinyobj::real_t vz = attrib.vertices[3*size_t(idx.vertex_index)+2];

                glm::vec3 normal{};

                // Check if `normal_index` is zero or positive. negative = no normal data
                if (idx.normal_index >= 0) {
                    tinyobj::real_t nx = attrib.normals[3*size_t(idx.normal_index)+0];
                    tinyobj::real_t ny = attrib.normals[3*size_t(idx.normal_index)+1];
                    tinyobj::real_t nz = attrib.normals[3*size_t(idx.normal_index)+2];
                    normal = {nx, ny, nz};
                    has_normals = true;
                }

                glm::vec2 uv{};

                // Check if `texcoord_index` is zero or positive. negative = no texcoord data
                if (idx.texcoord_index >= 0) {
                    tinyobj::real_t tx = attrib.texcoords[2*size_t(idx.texcoord_index)+0];
                    tinyobj::real_t ty = attrib.texcoords[2*size_t(idx.texcoord_index)+1];
                    uv = {tx, ty};
                }

                // Optional: vertex colors
                // tinyobj::real_t red   = attrib.colors[3*size_t(idx.vertex_index)+0];
                // tinyobj::real_t green = attrib.colors[3*size_t(idx.vertex_index)+1];
                // tinyobj::real_t blue  = attrib.colors[3*size_t(idx.vertex_index)+2];

                vertices.push_back({{vx, vy, vz}, normal, color, uv});
            }

            if (!has_normals) {
                glm::vec3 v0 = vertices[index_offset].pos;
                glm::vec3 v1 = vertices[index_offset + 1].pos;
                glm::vec3 v2 = vertices[index_offset + 2].pos;

                // Calculate the two edges of the triangle
                glm::vec3 edge1 = v1 - v0;
                glm::vec3 edge2 = v2 - v0;

                // Calculate the normal using the cross product
                glm::vec3 normal = glm::normalize(glm::cross(edge1, edge2));
                vertices[index_offset].normal = normal;
                vertices[index_offset + 1].normal = normal;
                vertices[index_offset + 2].normal = normal;
            }

            index_offset += fv;
        }
    }

    return {vertices, diffuseTexture};
}
