#pragma once

#include <filesystem>
#include <tinyexr.h>
#include <iostream>
#include <ktx.h>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include "ImageFunctions.h"

enum CubemapFace {
    POSITIVE_X = 0,
    NEGATIVE_X = 1,
    POSITIVE_Y = 2,
    NEGATIVE_Y = 3,
    POSITIVE_Z = 4,
    NEGATIVE_Z = 5
};

// Convert 3D direction vector to 2D equirectangular coordinates
void directionToEquirectangular(float x, float y, float z, float& u, float& v) {
    // Calculate spherical coordinates
    // x, y, z aligned with default camera orientation so that the resulting coordinates point to a center of panorama
    float phi = atan2(x, -z); // range (-pi, pi]
    float theta = asin(-y); // range (-pi/2, pi/2)
    
    // Map to UV coordinates [0,1] where (0, 0) is the top-left corner
    u = (phi + M_PI) / (2.0f * M_PI);
    v = (theta + M_PI / 2.0f) / M_PI;
}

// Top-left corner: (x, y) = (0, 0)
// Returns direction vector in world-space
glm::vec3 facePointToDirection(CubemapFace face, int faceSize, int x, int y) {
    // uv = (-1, -1) is a top-left corner of the face looking on it from outside of the cube
    float u = (x + 0.5f) * 2 / faceSize - 1;  // [-1, 1]
    float v = (y + 0.5f) * 2 / faceSize - 1;  // [-1, 1]

    glm::vec3 dir;

    switch (face) {
        case POSITIVE_X:
            dir.x = 1.0f;
            dir.y = -v;
            dir.z = -u;
            break;
        case NEGATIVE_X:
            dir.x = -1.0f;
            dir.y = -v;
            dir.z = u;
            break;
        case POSITIVE_Y:
            dir.x = u;
            dir.y = 1.0f;
            dir.z = v;
            break;
        case NEGATIVE_Y:
            dir.x = u;
            dir.y = -1.0f;
            dir.z = -v;
            break;
        case POSITIVE_Z:
            dir.x = u;
            dir.y = -v;
            dir.z = 1.0f;
            break;
        case NEGATIVE_Z:
            dir.x = -u;
            dir.y = -v;
            dir.z = -1.0f;
            break;
    }
    
    // Normalize the direction vector
    float len = sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    dir.x /= len;
    dir.y /= len;
    dir.z /= len;

    return dir;
}

// Sample image with bilinear interpolation
// u [0, 1]
// v [0, 1]
// uv = (0, 0) is the top-left corner
void sampleImage(const float* image, int width, int height, float u, float v, float* rgba) {
    // Convert to pixel coordinates
    float x = u * (width - 1);
    float y = v * (height - 1);
    
    // Get integer and fractional parts
    int x0 = static_cast<int>(floor(x));
    int y0 = static_cast<int>(floor(y));
    int x1 = (x0 + 1) % width;
    int y1 = fmin(y0 + 1, height - 1);
    
    float fx = x - x0;
    float fy = y - y0;
    
    // Read four nearest pixels
    float rgba00[4], rgba01[4], rgba10[4], rgba11[4];
    for (int i = 0; i < 4; i++) {
        rgba00[i] = image[(y0 * width + x0) * 4 + i];
        rgba01[i] = image[(y0 * width + x1) * 4 + i];
        rgba10[i] = image[(y1 * width + x0) * 4 + i];
        rgba11[i] = image[(y1 * width + x1) * 4 + i];
    }
    
    // Bilinear interpolation
    for (int i = 0; i < 4; i++) {
        float top = rgba00[i] * (1 - fx) + rgba01[i] * fx;
        float bottom = rgba10[i] * (1 - fx) + rgba11[i] * fx;
        rgba[i] = top * (1 - fy) + bottom * fy;
    }
}

glm::vec3 sampleEquirectangular(ImageData const& image, glm::vec3 dir) {
    float u, v;
    directionToEquirectangular(dir.x, dir.y, dir.z, u, v);
    float rgba[4];
    float* data = static_cast<float*>(image.data.get());
    sampleImage(data, image.width, image.height, u, v, rgba);
    return glm::vec3(rgba[0], rgba[1], rgba[2]);
}

std::vector<float> convertEquirectangularToCubemap(ImageData const& image, int faceSize) {
    // Allocate memory for 6 faces * faceSize^2 * 3 channels (RGB)
    std::vector<float> cubemapData(6 * faceSize * faceSize * 3);

    for (ktx_uint32_t faceSlice = 0; faceSlice < 6; faceSlice++) {
        CubemapFace face = static_cast<CubemapFace>(faceSlice);
        for (int y = 0; y < faceSize; y++) {
            for (int x = 0; x < faceSize; x++) {
                glm::vec3 dir = facePointToDirection(face, faceSize, x, y);
                glm::vec3 rgb = sampleEquirectangular(image, dir);

                int pixelIndex = (face * faceSize * faceSize + y * faceSize + x) * 3;
                cubemapData[pixelIndex + 0] = rgb.r;
                cubemapData[pixelIndex + 1] = rgb.g;
                cubemapData[pixelIndex + 2] = rgb.b;
            }
        }
    }

    return cubemapData;
}

glm::vec3 worldDirFromSphericalCoordinates(float sinTheta, float cosTheta, float sinPhi, float cosPhi) {
    return {
        -sinTheta * sinPhi,
        cosTheta,
        sinTheta * cosPhi,
    };
}

glm::vec3 worldDirFromSphericalCoordinates(float theta, float phi) {
    return worldDirFromSphericalCoordinates(
        std::sin(theta),
        std::cos(theta),
        std::sin(phi),
        std::cos(phi)
    );
}

glm::vec3 worldDirFromEquirectangularUV(float u, float v) {
    // Both u and v should be in the range [0, 1]
    float theta = v * M_PI;
    float phi = u * 2.0f * M_PI;
    return worldDirFromSphericalCoordinates(theta, phi);
}

glm::vec3 worldDirFromEquirectangularCoordinates(int x, int y, int width, int height) {
    float u = (x + 0.5f) / float(width);
    float v = (y + 0.5f) / float(height);
    return worldDirFromEquirectangularUV(u, v);
}

// Calculate spherical harmonics from equirectangular environment map
std::vector<glm::vec3> calculateDiffuseSphericalHarmonics(ImageData const& image) {
    float* equiRgba = static_cast<float*>(image.data.get());
    int width = image.width;
    int height = image.height;

    // 9 SH coefficients (3 bands) for each of RGB
    std::vector<glm::vec3> shCoeffs(9, glm::vec3(0.0f));
    
    const float dTheta = M_PI / float(height);
    const float dPhi = 2.0f * M_PI / float(width);

    // Loop through every pixel in the equirectangular map
    for (int y = 0; y < height; ++y) {
        float v = (y + 0.5f) / float(height); // [0,1]
        float theta = v * M_PI; // [0, PI]

        float sinTheta = std::sin(theta);
        float cosTheta = std::cos(theta);

        for (int x = 0; x < width; ++x) {
            float u = (x + 0.5f) / float(width); // [0,1]
            float phi = u * 2.0f * M_PI; // [0, 2PI]

            float sinPhi = std::sin(phi);
            float cosPhi = std::cos(phi);

            glm::vec3 dir = worldDirFromSphericalCoordinates(sinTheta, cosTheta, sinPhi, cosPhi);

            // Get color from equirectangular image
            int pixelIndex = (y * width + x) * 4;
            glm::vec3 color(
                equiRgba[pixelIndex + 0],
                equiRgba[pixelIndex + 1],
                equiRgba[pixelIndex + 2]
            );

            // Differential solid angle of the texel
            // sin(θ) comes from Jacobian of spherical coordinates
            float dOmega = dTheta * dPhi * sinTheta;

            // Evaluate SH basis functions (real, normalized)
            float Y[9] = {};
            Y[0] = 0.282095f;
            Y[1] = 0.488603f * dir.y;
            Y[2] = 0.488603f * dir.z;
            Y[3] = 0.488603f * dir.x;
            Y[4] = 1.092548f * dir.x * dir.y;
            Y[5] = 1.092548f * dir.y * dir.z;
            Y[6] = 0.315392f * (3.0f * dir.z * dir.z - 1.0f);
            Y[7] = 1.092548f * dir.x * dir.z;
            Y[8] = 0.546274f * (dir.x * dir.x - dir.y * dir.y);

            // Accumulate SH coefficients
            for (int i = 0; i < 9; ++i) {
                shCoeffs[i] += color * Y[i] * dOmega;
            }
        }
    }

    // Apply analytic convolution of Lambertian BRDF (cosθ/π) with SH basis. This transforms radiance to irradiance.
    // These weights come from projecting cosine lobe onto SH basis functions.
    shCoeffs[0] *= M_PI;
    for (int i = 1; i <= 3; ++i) shCoeffs[i] *= (2.0 * M_PI) / 3.0;
    for (int i = 4; i <= 8; ++i) shCoeffs[i] *= M_PI / 4.0;

    // Convert to reflected radiance (dividing by π according to Lambertian model).
    for (auto& c : shCoeffs) c /= M_PI;

    return shCoeffs;
}

int calculateDiffuseSphericalHarmonics(ImageData const& image, const char* outputFileName) {
    auto shCoeffs = calculateDiffuseSphericalHarmonics(image);
    std::ofstream outFile(outputFileName);
    if (!outFile) {
        std::cerr << "Error: Could not open file for writing: " << outputFileName << std::endl;
        return -1;
    }
    
    outFile << shCoeffs.size() << std::endl;
    
    for (const auto& coeff : shCoeffs) {
        outFile << coeff.x << " " << coeff.y << " " << coeff.z << std::endl;
    }
    
    outFile.close();
    return 0;
}

std::vector<glm::vec3> loadSHCoeffs(const char* filename) {
    std::ifstream inFile(filename);
    if (!inFile) {
        std::cerr << "Error: Could not open file for reading: " << filename << std::endl;
        return {};
    }
    
    size_t numCoeffs;
    inFile >> numCoeffs;
    
    std::vector<glm::vec3> shCoeffs;
    shCoeffs.reserve(numCoeffs);
    
    for (size_t i = 0; i < numCoeffs; ++i) {
        float x, y, z;
        inFile >> x >> y >> z;
        shCoeffs.emplace_back(x, y, z);
    }
    
    inFile.close();
    return shCoeffs;
}

glm::vec3 importanceSampleGGX(const ImageData& equirectangularImage, const glm::vec3& normal, float roughness, int sampleCount) {
    glm::vec3 color(0.0f);
    float totalWeight = 0.0f;

    // Create tangent space basis
    glm::vec3 up = abs(normal.z) < 0.999f ? glm::vec3(0, 0, 1) : glm::vec3(1, 0, 0);
    glm::vec3 tangent = glm::normalize(glm::cross(up, normal));
    glm::vec3 bitangent = glm::cross(normal, tangent);
    
    float alpha = roughness * roughness;
    
    for (int i = 0; i < sampleCount; ++i) {
        // TODO: use a proper random number generator
        float xi1 = static_cast<float>(rand()) / RAND_MAX;
        float xi2 = static_cast<float>(rand()) / RAND_MAX;
        
        // Importance sample GGX distribution
        float phi = 2.0f * M_PI * xi1;
        float cosTheta = sqrt((1.0f - xi2) / (1.0f + (alpha * alpha - 1.0f) * xi2));
        float sinTheta = sqrt(1.0f - cosTheta * cosTheta);
        
        // Convert to Cartesian coordinates in tangent space
        glm::vec3 halfVector(
            sinTheta * cos(phi),
            sinTheta * sin(phi),
            cosTheta
        );
        
        // Transform to world space
        glm::vec3 sampleDir = halfVector.x * tangent + halfVector.y * bitangent + halfVector.z * normal;
        
        // Calculate reflection direction
        glm::vec3 lightDir = glm::normalize(2.0f * glm::dot(sampleDir, normal) * sampleDir - normal);

        float NdotL = glm::max(glm::dot(normal, lightDir), 0.0f);
        if (NdotL > 0.0f) {
            glm::vec3 envColor = sampleEquirectangular(equirectangularImage, lightDir);
            color += envColor * NdotL;
            totalWeight += NdotL;
        }
    }
    
    return totalWeight > 0.0f ? color / totalWeight : glm::vec3(0.0f);
}

std::vector<float> filterCubemapForRoughness(const ImageData& equirectangularImage, int faceSize, float roughness, int sampleCount) {
    // Allocate memory for 6 faces * faceSize^2 * 3 channels (RGB)
    std::vector<float> cubemapData(6 * faceSize * faceSize * 3);
    
    // For each face of the cubemap
    for (int face = 0; face < 6; ++face) {
        for (int y = 0; y < faceSize; ++y) {
            for (int x = 0; x < faceSize; ++x) {
                glm::vec3 dir = facePointToDirection(static_cast<CubemapFace>(face), faceSize, x, y);
                
                // Apply importance sampling based on GGX/Trowbridge-Reitz distribution
                glm::vec3 filteredColor = importanceSampleGGX(equirectangularImage, dir, roughness, sampleCount);
                
                // Store in cubemap data
                int pixelIndex = (face * faceSize * faceSize + y * faceSize + x) * 3;
                cubemapData[pixelIndex + 0] = filteredColor.r;
                cubemapData[pixelIndex + 1] = filteredColor.g;
                cubemapData[pixelIndex + 2] = filteredColor.b;
            }
        }
    }
    
    return cubemapData;
}

int saveCubemapMipsToKtx2(const std::vector<std::vector<float>>& mipData, const char* filename, int baseFaceSize) {
    ktxTexture2* texture;
    KTX_error_code result;
    
    ktxTextureCreateInfo createInfo = {
        .vkFormat = VK_FORMAT_R32G32B32A32_SFLOAT,
        .baseWidth = static_cast<ktx_uint32_t>(baseFaceSize),
        .baseHeight = static_cast<ktx_uint32_t>(baseFaceSize),
        .baseDepth = 1,
        .numDimensions = 2,
        .numLevels = static_cast<ktx_uint32_t>(mipData.size()),
        .numLayers = 1,
        .numFaces = 6,
        .isArray = false,
        .generateMipmaps = false,
    };
    
    result = ktxTexture2_Create(
        &createInfo,
        KTX_TEXTURE_CREATE_ALLOC_STORAGE,
        &texture
    );
    if (result != KTX_SUCCESS) {
        std::cerr << "Failed to create KTX2 texture: " << ktxErrorString(result) << std::endl;
        return -1;
    }
    
    // Process each mip level
    for (ktx_uint32_t mipLevel = 0; mipLevel < mipData.size(); ++mipLevel) {
        int currentFaceSize = baseFaceSize >> mipLevel; // Divide by 2^mipLevel
        if (currentFaceSize < 1) currentFaceSize = 1;
        
        // Convert RGB data to RGBA by adding alpha channel
        auto rgbaData = std::make_unique<float[]>(4 * currentFaceSize * currentFaceSize);
        
        // Process each face
        for (ktx_uint32_t face = 0; face < 6; ++face) {
            // Calculate source offset in RGB data (3 channels per pixel)
            int srcOffset = face * currentFaceSize * currentFaceSize * 3;
            
            // Convert RGB to RGBA for this face
            for (int pixel = 0; pixel < currentFaceSize * currentFaceSize; ++pixel) {
                int srcIdx = srcOffset + pixel * 3;
                int dstIdx = pixel * 4;
                
                rgbaData[dstIdx + 0] = mipData[mipLevel][srcIdx + 0]; // R
                rgbaData[dstIdx + 1] = mipData[mipLevel][srcIdx + 1]; // G
                rgbaData[dstIdx + 2] = mipData[mipLevel][srcIdx + 2]; // B
                rgbaData[dstIdx + 3] = 1.0f; // A (alpha = 1.0)
            }
            
            ktx_size_t faceDataSize = 4 * sizeof(float) * currentFaceSize * currentFaceSize;
            
            result = ktxTexture_SetImageFromMemory(
                ktxTexture(texture),
                mipLevel,
                0, // layer
                face,
                reinterpret_cast<ktx_uint8_t*>(rgbaData.get()),
                faceDataSize
            );
            
            if (result != KTX_SUCCESS) {
                std::cerr << "Failed to set image data for mip " << mipLevel 
                          << ", face " << face << ": " << ktxErrorString(result) << std::endl;
                ktxTexture_Destroy(ktxTexture(texture));
                return -1;
            }
        }
    }
    
    result = ktxTexture_WriteToNamedFile(ktxTexture(texture), filename);
    if (result != KTX_SUCCESS) {
        std::cerr << "Failed to write KTX2 file: " << ktxErrorString(result) << std::endl;
        ktxTexture_Destroy(ktxTexture(texture));
        return -1;
    }
    
    ktxTexture_Destroy(ktxTexture(texture));
    return 0;
}

int prefilterEnvmap(const ImageData& inputImage, const char* outputFileName, int baseFaceSize, int sampleCount) {
    // Calculate number of mip levels based on face size
    int numMipLevels = static_cast<int>(std::floor(std::log2(baseFaceSize))) + 1;
    
    // Create cubemap data for all mip levels
    std::vector<std::vector<float>> cubemapMips;
    cubemapMips.resize(numMipLevels);

    // Base level contains original map
    cubemapMips[0] = convertEquirectangularToCubemap(inputImage, baseFaceSize);
    
    // mips 1+ contain prefiltered data for specular reflections
    for (int mip = 1; mip < numMipLevels; ++mip) {
        int faceSize = baseFaceSize >> mip; // Divide by 2^mip
        if (faceSize < 1) faceSize = 1;
        
        // Calculate roughness for this mip level
        // Mip 0 = roughness 0 (mirror), higher mips = higher roughness
        float roughness = static_cast<float>(mip) / static_cast<float>(numMipLevels - 1);
        
        // Generate filtered cubemap for this roughness level
        std::vector<float> filteredCubemap = filterCubemapForRoughness(inputImage, faceSize, roughness, sampleCount);
        cubemapMips[mip] = std::move(filteredCubemap);
    }
    
    return saveCubemapMipsToKtx2(cubemapMips, outputFileName, baseFaceSize);
}
