#pragma once

#include <string>
#include <iostream>
#include <stb_image.h>
#include <tinyexr.h>
#include <vulkan/vulkan.h>

struct ImageData {
    std::unique_ptr<void, decltype(&free)> data{nullptr, free};
    size_t dataSize;
    VkFormat imageFormat;
    int width;
    int height;
};

ImageData loadImage(std::string const& filename) {
    if (filename.ends_with(".exr")) {
        float *rgba = nullptr;
        int width, height;
        const char *err = nullptr;
        int ret = LoadEXR(&rgba, &width, &height, filename.c_str(), &err);
        if (TINYEXR_SUCCESS != ret) {
            std::cerr << "Failed to load EXR file [" << filename << "] code = " << ret << std::endl;
            if (err) {
                std::cerr << err << std::endl;
                FreeEXRErrorMessage(err);
            }
            
            throw std::runtime_error(std::string("failed to load texture image! ") + filename);
        }

        // reinhard tonemapping
        // for (int i = 0; i < width * height * 4; i++) {
            // rgba[i] = rgba[i] / (1 + rgba[i]);
        // }

        // convert to 8-bit range
        // size_t dataSize = static_cast<size_t>(width * height * 4);
        // uint8_t *rgba8 = (uint8_t*) malloc(dataSize);
        // for (size_t i = 0; i < dataSize; ++i) {
            // rgba8[i] = static_cast<uint8_t> (rgba[i] * 255);
        // }
        // free(rgba);
        
        ImageData result;
        result.data.reset(rgba);
        result.dataSize = width * height * 4 * 4;
        result.imageFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
        result.width = width;
        result.height = height;
        return result;
    }
    else {
        int texWidth, texHeight, texChannels;
        stbi_uc* pixels = stbi_load(filename.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

        if (!pixels) {
            throw std::runtime_error(std::string("failed to load texture image! ") + filename);
        }

        ImageData result;
        result.data.reset(pixels);
        result.dataSize = static_cast<size_t>(texWidth * texHeight * 4);
        result.imageFormat = VK_FORMAT_R8G8B8A8_SRGB;
        result.width = texWidth;
        result.height = texHeight;
        return result;
    }
}
