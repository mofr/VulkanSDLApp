#pragma once

#include <glm/glm.hpp>
#include "CubemapFunctions.h"
#include "CircleRange.h"

struct ExtractedSunData {
    glm::vec3 dir = {};  // Direction of the sun, not direction to the sun
    glm::vec3 radiance = {};
    float solidAngle = 0;
    const char* error = nullptr;
};

ExtractedSunData extractSunFromEquirectangularPanorama(ImageData& image, float sunSolidAngle) {
    float* equiRgba = static_cast<float*>(image.data.get());
    float maxRadiance = 0;
    int maxRadianceX = 0;
    int maxRadianceY = 0;
    for (int y = 0; y < image.height; y++) {
        for (int x = 0; x < image.width; x++) {
            float* texel = equiRgba + (y * image.width + x) * 4;
            float radiance = texel[0] + texel[1] + texel[2];
            if (radiance > maxRadiance) {
                maxRadiance = radiance;
                maxRadianceX = x;
                maxRadianceY = y;
            }
        }
    }
    if (maxRadiance == 0) {
        return {.error="The input image is completely black"};
    }
    float minRadiance = maxRadiance;
    glm::vec3 minRadianceTexel;
    for (auto [x, y] : equirectangularCircle(maxRadianceX, maxRadianceY, image.width, image.height, sunSolidAngle)) {
        float* texel = equiRgba + (y * image.width + x) * 4;
        float radiance = texel[0] + texel[1] + texel[2];
        if (radiance < minRadiance) {
            minRadiance = radiance;
            minRadianceTexel = {texel[0], texel[1], texel[2]};
        }
    }
    glm::vec3 extractedRadianceSum;
    int totalTexels = 0;
    for (auto [x, y] : equirectangularCircle(maxRadianceX, maxRadianceY, image.width, image.height, sunSolidAngle)) {
        float* texel = equiRgba + (y * image.width + x) * 4;
        extractedRadianceSum += glm::vec3{texel[0], texel[1], texel[2]} - minRadianceTexel;
        texel[0] = minRadianceTexel.r;
        texel[1] = minRadianceTexel.g;
        texel[2] = minRadianceTexel.b;
        totalTexels++;
    }
    glm::vec3 sunRadiance = extractedRadianceSum / static_cast<float>(totalTexels);
    ExtractedSunData sunData;
    sunData.dir = worldDirFromEquirectangularCoordinates(maxRadianceX, maxRadianceY, image.width, image.height);
    sunData.dir = -sunData.dir; // Make it direction of the sun
    sunData.radiance = sunRadiance;
    sunData.solidAngle = sunSolidAngle;
    return sunData;
}
