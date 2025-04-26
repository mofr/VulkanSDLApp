#pragma once

#include <array>
#include <string>
#include <vulkan/vulkan.h>
#include <imgui.h>

struct RenderingConfig {
    VkSurfaceFormatKHR surfaceFormat;
    bool vsyncEnabled = true;
    float maxAnisotropy = 0;
    VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;
    bool useMipMaps = true;
    int environmentIndex = 0;
};

struct RenderingConfigOptions {
    VkPhysicalDeviceProperties const& physicalDeviceProperties;
    std::span<const char*> environments;
    std::span<VkSurfaceFormatKHR> surfaceFormats;
    std::span<const char*> surfaceFormatLabels;
};

bool operator == (VkSurfaceFormatKHR f1, VkSurfaceFormatKHR f2) {
    return f1.format == f2.format && f1.colorSpace == f2.colorSpace;
}

const char* getSurfaceFormatLabel(VkSurfaceFormatKHR surfaceFormat) {
    if (surfaceFormat == VkSurfaceFormatKHR{ VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT })
        return "16 bit Display P3";
    if (surfaceFormat == VkSurfaceFormatKHR{ VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_BT2020_LINEAR_EXT })
        return "16 bit BT.2020";
    if (surfaceFormat == VkSurfaceFormatKHR{ VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT })
        return "16 bit Extended sRGB";
    if (surfaceFormat == VkSurfaceFormatKHR{ VK_FORMAT_A2R10G10B10_UNORM_PACK32, VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT })
        return "10 bit Display P3";
    if (surfaceFormat == VkSurfaceFormatKHR{ VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT })
        return "10 bit Display P3 (BGR)";
    if (surfaceFormat == VkSurfaceFormatKHR{ VK_FORMAT_R8G8B8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
        return "8 bit sRGB";
    if (surfaceFormat == VkSurfaceFormatKHR{ VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
        return "8 bit sRGB (BGR)";
    return "Unknown format";
}

bool renderingConfigGui(
    RenderingConfig& config,
    RenderingConfigOptions const& options,
    float dt
) {
    auto fps = static_cast<int>(1.0f / dt);
    bool changed = false;

    ImGui::Begin("Config");
    std::string frameTimeString = std::to_string(dt * 1000) + " ms";
    std::string fpsString = std::to_string(fps) + " FPS";
    ImGui::Text("%s", frameTimeString.c_str());
    ImGui::Text("%s", fpsString.c_str());
    
    {
        if (ImGui::BeginCombo("Surface Format", getSurfaceFormatLabel(config.surfaceFormat))) {
            for (size_t i = 0; i < options.surfaceFormats.size(); ++i) {
                auto format = options.surfaceFormats[i];
                auto label = getSurfaceFormatLabel(format);
                bool selected = format.format == config.surfaceFormat.format && format.colorSpace == config.surfaceFormat.colorSpace;
                if (ImGui::Selectable(label, selected)) {
                    changed = true;
                    config.surfaceFormat = format;
                }
            }
            ImGui::EndCombo();
        }
    }

    changed |= ImGui::Checkbox("VSync", &config.vsyncEnabled);
    changed |= ImGui::Checkbox("Use mipmaps", &config.useMipMaps);

    {
        static const std::array labels = { "Trilinear", "2X", "4X", "8X", "16X" };
        static const std::array values = { 0.0f, 2.0f, 4.0f, 8.0f, 16.0f };
        int count = values.size();
        int elem = 0;
        for (elem = 0; elem < count; ++elem) {
            if (config.maxAnisotropy == values[elem]) break;
        }
        if (ImGui::SliderInt("Anisotropy", &elem, 0, count - 1, labels[elem])) {
            changed = true;
            config.maxAnisotropy = values[elem];
        }
    }
    {
        static const std::array labels = { "Off", "2", "4", "8", "16", "32", "64" };
        static const std::array values = { 
            VK_SAMPLE_COUNT_1_BIT,
            VK_SAMPLE_COUNT_2_BIT,
            VK_SAMPLE_COUNT_4_BIT,
            VK_SAMPLE_COUNT_8_BIT,
            VK_SAMPLE_COUNT_16_BIT,
            VK_SAMPLE_COUNT_32_BIT,
            VK_SAMPLE_COUNT_64_BIT
        };
        int elemCount = 1;
        VkSampleCountFlags counts = options.physicalDeviceProperties.limits.framebufferColorSampleCounts & options.physicalDeviceProperties.limits.framebufferDepthSampleCounts;
        if (counts & VK_SAMPLE_COUNT_2_BIT) { elemCount = 2; }
        if (counts & VK_SAMPLE_COUNT_4_BIT) { elemCount = 3; }
        if (counts & VK_SAMPLE_COUNT_8_BIT) { elemCount = 4; }
        if (counts & VK_SAMPLE_COUNT_16_BIT) { elemCount = 5; }
        if (counts & VK_SAMPLE_COUNT_32_BIT) { elemCount = 6; }
        if (counts & VK_SAMPLE_COUNT_64_BIT) { elemCount = 7; }
        int elem = 0;
        for (elem = 0; elem < elemCount; ++elem) {
            if (config.msaaSamples == values[elem]) break;
        }
        if (ImGui::SliderInt("Multisampling", &elem, 0, elemCount - 1, labels[elem])) {
            changed = true;
            config.msaaSamples = values[elem];
        }
    }
    {
        if (ImGui::BeginCombo("Environment", options.environments[config.environmentIndex])) {
            for (size_t i = 0; i < options.environments.size(); ++i) {
                if (ImGui::Selectable(options.environments[i], i == static_cast<size_t>(config.environmentIndex))) {
                    changed = true;
                    config.environmentIndex = i;
                }
            }
            ImGui::EndCombo();
        }
    }
    ImGui::End();

    return changed;
}
