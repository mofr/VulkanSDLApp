#pragma once

#include <array>
#include <string>
#include <vulkan/vulkan.h>
#include <imgui.h>

struct RenderingConfig {
    bool vsyncEnabled = true;
    float maxAnisotropy = 0;
    VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;
    bool useMipMaps = true;
};

bool renderingConfigGui(RenderingConfig& config, float dt, VkPhysicalDeviceProperties const& physicalDeviceProperties) {
    auto fps = static_cast<int>(1.0f / dt);
    bool changed = false;

    ImGui::Begin("Config");
    std::string frameTimeString = std::to_string(dt * 1000) + " ms";
    std::string fpsString = std::to_string(fps) + " FPS";
    ImGui::Text("%s", frameTimeString.c_str());
    ImGui::Text("%s", fpsString.c_str());
    
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
        VkSampleCountFlags counts = physicalDeviceProperties.limits.framebufferColorSampleCounts & physicalDeviceProperties.limits.framebufferDepthSampleCounts;
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
    ImGui::End();

    return changed;
}
