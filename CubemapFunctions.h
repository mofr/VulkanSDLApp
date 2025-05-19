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
void facePointToDirection(CubemapFace face, int faceSize, int x, int y, float& dirX, float& dirY, float& dirZ) {
    // uv = (-1, -1) is a top-left corner of the face looking on it from outside of the cube
    float u = (x + 0.5f) * 2 / faceSize - 1;  // [-1, 1]
    float v = (y + 0.5f) * 2 / faceSize - 1;  // [-1, 1]

    switch (face) {
        case POSITIVE_X:
            dirX = 1.0f;
            dirY = -v;
            dirZ = -u;
            break;
        case NEGATIVE_X:
            dirX = -1.0f;
            dirY = -v;
            dirZ = u;
            break;
        case POSITIVE_Y:
            dirX = u;
            dirY = 1.0f;
            dirZ = v;
            break;
        case NEGATIVE_Y:
            dirX = u;
            dirY = -1.0f;
            dirZ = -v;
            break;
        case POSITIVE_Z:
            dirX = u;
            dirY = -v;
            dirZ = 1.0f;
            break;
        case NEGATIVE_Z:
            dirX = -u;
            dirY = -v;
            dirZ = -1.0f;
            break;
    }
    
    // Normalize the direction vector
    float len = sqrt(dirX * dirX + dirY * dirY + dirZ * dirZ);
    dirX /= len;
    dirY /= len;
    dirZ /= len;
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

void equirectangularToCubemapFace(
    float* input,
    int inputWidth,
    int inputHeight,
    float* faceData,
    int faceSize,
    CubemapFace face
) {
    for (int y = 0; y < faceSize; y++) {
        for (int x = 0; x < faceSize; x++) {
            float dirX, dirY, dirZ;
            facePointToDirection(face, faceSize, x, y, dirX, dirY, dirZ);
            float u, v;
            directionToEquirectangular(dirX, dirY, dirZ, u, v);
            float rgba[4];
            sampleImage(input, inputWidth, inputHeight, u, v, rgba);

            *(faceData++) = rgba[0];
            *(faceData++) = rgba[1];
            *(faceData++) = rgba[2];
            *(faceData++) = rgba[3];
        }
    }
}

int convertEquirectangularToCubemap(ImageData const& image, const char* outputDir, int faceSize) {
    float* equiRgba = static_cast<float*>(image.data.get());
    int width = image.width;
    int height = image.height;

    auto outputData = std::make_unique<float[]>(faceSize * faceSize * 4);
    std::filesystem::path outputDirPath = outputDir;
    std::filesystem::create_directories(outputDirPath);
    static std::array faceNames = {"px", "nx", "py", "ny", "pz", "nz"};

    for (int faceIndex = 0; faceIndex < 6; faceIndex++) {
        equirectangularToCubemapFace(equiRgba, width, height, outputData.get(), faceSize, static_cast<CubemapFace>(faceIndex));
        std::string filename = std::string(faceNames[faceIndex]) + ".exr";
        std::filesystem::path outputFilepath = outputDirPath / filename;
        const char* err = nullptr;
        int saveExrResult = SaveEXR(outputData.get(), faceSize, faceSize, 4, 0, outputFilepath.c_str(), &err);
        if (TINYEXR_SUCCESS != saveExrResult) {
            std::cerr << "Failed to save EXR file '" << outputFilepath << "' code = " << saveExrResult << std::endl;
            if (err) {
                std::cerr << err << std::endl;
                FreeEXRErrorMessage(err);
            }
            return -1;
        }
    }
    return 0;
}

int convertEquirectangularToCubemapKtx(ImageData const& image, const char* outputFileName, int faceSize) {
    float* equiRgba = static_cast<float*>(image.data.get());
    int width = image.width;
    int height = image.height;

    ktxTexture2* texture;
    KTX_error_code result;
    
    ktxTextureCreateInfo createInfo = {
        .vkFormat = VK_FORMAT_R32G32B32A32_SFLOAT,
        .baseWidth = static_cast<ktx_uint32_t>(faceSize),
        .baseHeight = static_cast<ktx_uint32_t>(faceSize),
        .baseDepth = 1,
        .numDimensions = 2,
        .numLevels = 1,
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
        std::cerr << ktxErrorString(result) << std::endl;
        return -1;
    }
    
    ktx_size_t srcSizeInBytes = 4 * 4 * faceSize * faceSize;
    auto outputData = std::make_unique<float[]>(4 * faceSize * faceSize);
    ktx_uint32_t level = 0;
    ktx_uint32_t layer = 0;
    for (ktx_uint32_t faceSlice = 0; faceSlice < 6; faceSlice++) {
        equirectangularToCubemapFace(equiRgba, width, height, outputData.get(), faceSize, static_cast<CubemapFace>(faceSlice));
        result = ktxTexture_SetImageFromMemory(
            ktxTexture(texture),
            level,
            layer,
            faceSlice,
            (ktx_uint8_t*)outputData.get(),
            srcSizeInBytes
        );
        if (result != KTX_SUCCESS) {
            std::cerr << ktxErrorString(result) << std::endl;
            return -1;
        }
    }

    ktxTexture_WriteToNamedFile(ktxTexture(texture), outputFileName);
    ktxTexture_Destroy(ktxTexture(texture));
    return 0;
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
