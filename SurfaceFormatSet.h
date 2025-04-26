#pragma once

#include <vulkan/vulkan.h>
#include <unordered_set>

struct VkSurfaceFormatKHRHash {
    size_t operator()(const VkSurfaceFormatKHR& format) const {
        // Same hash as before
        return std::hash<uint32_t>()(format.format) ^ 
              (std::hash<uint32_t>()(format.colorSpace) << 1);
    }
};

typedef std::unordered_set<VkSurfaceFormatKHR, VkSurfaceFormatKHRHash> SurfaceFormatSet;
