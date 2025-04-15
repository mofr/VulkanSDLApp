#pragma once

#include <filesystem>
#include <tinyexr.h>
#include <iostream>

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
    float phi = atan2(z, x);
    float theta = asin(y);
    
    // Map to UV coordinates [0,1]
    u = (phi + M_PI) / (2.0f * M_PI);
    v = (theta + M_PI / 2.0f) / M_PI;
}

void facePointToDirection(CubemapFace face, int faceSize, int x, int y, float& dirX, float& dirY, float& dirZ) {
    float u = (x + 0.5f) * 2 / faceSize - 1;  // [-1, 1]
    float v = (y + 0.5f) * 2 / faceSize - 1;  // [-1, 1]

    switch (face) {
        case POSITIVE_X:
            dirX = 1.0f;
            dirY = v;
            dirZ = -u;
            break;
        case NEGATIVE_X:
            dirX = -1.0f;
            dirY = v;
            dirZ = u;
            break;
        case POSITIVE_Y:
            dirX = u;
            dirY = -1.0f;
            dirZ = v;
            break;
        case NEGATIVE_Y:
            dirX = u;
            dirY = 1.0f;
            dirZ = -v;
            break;
        case POSITIVE_Z:
            dirX = u;
            dirY = v;
            dirZ = 1.0f;
            break;
        case NEGATIVE_Z:
            dirX = -u;
            dirY = v;
            dirZ = -1.0f;
            break;
    }
    
    // Normalize the direction vector
    float len = sqrt(dirX * dirX + dirY * dirY + dirZ * dirZ);
    dirX /= len;
    dirY /= len;
    dirZ /= len;
}

// Sample equirectangular texture with bilinear interpolation
void sampleEquirectangular(const float* equirectangular, int width, int height, float u, float v, float* rgba) {
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
        rgba00[i] = equirectangular[(y0 * width + x0) * 4 + i];
        rgba01[i] = equirectangular[(y0 * width + x1) * 4 + i];
        rgba10[i] = equirectangular[(y1 * width + x0) * 4 + i];
        rgba11[i] = equirectangular[(y1 * width + x1) * 4 + i];
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
            float equiU, equiV;
            directionToEquirectangular(dirX, dirY, dirZ, equiU, equiV);
            float rgba[4];
            sampleEquirectangular(input, inputWidth, inputHeight, equiU, equiV, rgba);

            *(faceData++) = rgba[0];
            *(faceData++) = rgba[1];
            *(faceData++) = rgba[2];
            *(faceData++) = rgba[3];
        }
    }
}

int convertEquirectangularToCubemap(const char* inputFilename, const char* outputDir, int faceSize) {
    float* rgba = nullptr;
    int width, height;
    {
        const char *err = nullptr;
        int loadExrResult = LoadEXR(&rgba, &width, &height, inputFilename, &err);
        if (TINYEXR_SUCCESS != loadExrResult) {
            std::cerr << "Failed to load EXR file '" << inputFilename << "' code = " << loadExrResult << std::endl;
            if (err) {
                std::cerr << err << std::endl;
                FreeEXRErrorMessage(err);
            }
            return -1;
        }
    }

    auto outputData = std::make_unique<float[]>(faceSize * faceSize * 4);
    std::filesystem::path outputDirPath = outputDir;
    std::filesystem::create_directories(outputDirPath);
    static std::array faceNames = {"px", "nx", "py", "ny", "pz", "nz"};

    for (int faceIndex = 0; faceIndex < 6; faceIndex++) {
        equirectangularToCubemapFace(rgba, width, height, outputData.get(), faceSize, static_cast<CubemapFace>(faceIndex));
        std::string filename = std::string(faceNames[faceIndex]) + ".exr";
        std::filesystem::path outputFilepath = outputDirPath / filename;
        const char* err = nullptr;
        int saveExrResult = SaveEXR(outputData.get(), faceSize, faceSize, 4, 0, outputFilepath.c_str(), &err);
        if (TINYEXR_SUCCESS != saveExrResult) {
            std::cerr << "Failed to save EXR file '" << inputFilename << "' code = " << saveExrResult << std::endl;
            if (err) {
                std::cerr << err << std::endl;
                FreeEXRErrorMessage(err);
            }
            return -1;
        }
    }
    return 0;
}