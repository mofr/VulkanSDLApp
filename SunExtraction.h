#pragma once

#include <glm/glm.hpp>
#include "CubemapFunctions.h"

struct ExtractedSunData {
    glm::vec3 dir = {};  // Direction of the sun, not direction to the sun
    glm::vec3 radiance = {};
    const char* error = nullptr;
};

ExtractedSunData extractSunFromEquirectangularPanorama(ImageData& image) {
    float* equiRgba = static_cast<float*>(image.data.get());
    float maxRadiance = 0;
    int maxRadianceX = 0;
    int maxRadianceY = 0;
    float* center = nullptr;
    for (int y = 0; y < image.height; y++) {
        for (int x = 0; x < image.width; x++) {
            float* texel = equiRgba + (y * image.width + x) * 4;
            float radiance = texel[0] + texel[1] + texel[2];
            if (radiance > maxRadiance) {
                maxRadiance = radiance;
                maxRadianceX = x;
                maxRadianceY = y;
                center = texel;
            }
        }
    }
    if (maxRadiance == 0) {
        return {.error="The input image is completely black"};
    }
    glm::vec3 sunRadiance = {center[0], center[1], center[2]};
    center[0] = 0;
    center[1] = 0;
    center[2] = 0;
    center[3] = 0;
    ExtractedSunData sunData;
    sunData.dir = worldDirFromEquirectangularCoordinates(maxRadianceX, maxRadianceY, image.width, image.height);
    sunData.dir = -sunData.dir; // Make it direction of the sun
    sunData.radiance = sunRadiance;
    return sunData;
}
