#pragma once

#include <vector>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_metal.h>

#include "VulkanFunctions.h"

class VulkanContext {
public:
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkPhysicalDeviceProperties physicalDeviceProperties;
    VkDevice device;
    uint32_t graphicsQueueFamilyIndex;
    VkQueue graphicsQueue;
    VkCommandPool commandPool;

    VulkanContext() {
        std::vector extensions = {
            VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
            VK_EXT_METAL_SURFACE_EXTENSION_NAME,
            VK_KHR_SURFACE_EXTENSION_NAME,
            "VK_KHR_get_physical_device_properties2",
            "VK_EXT_swapchain_colorspace",
            // VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME,
            // "VK_KHR_get_surface_capabilities2",
        };
    
        const std::vector<const char*> validationLayers = {
            "VK_LAYER_KHRONOS_validation"
        };
        if (!checkValidationLayerSupport(validationLayers)) {
            throw std::runtime_error("Validation layers requested, but not available!");
        }
    
        {
            VkInstanceCreateInfo createInfo{
                .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                .flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR,
                .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
                .ppEnabledExtensionNames = extensions.data(),
                .enabledLayerCount = static_cast<uint32_t>(validationLayers.size()),
                .ppEnabledLayerNames = validationLayers.data(),
            };
    
            VkResult result = vkCreateInstance(&createInfo, nullptr, &instance);
            if (result != VK_SUCCESS) {
                throw std::runtime_error("Failed to create Vulkan instance!");
            }
        }
    
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
        std::vector<VkPhysicalDevice> physicalDevices(deviceCount);
        vkEnumeratePhysicalDevices(instance, &deviceCount, physicalDevices.data());
    
        for (const auto& device : physicalDevices) {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(device, &props);
            static std::array typeStrings = {
                "Other",
                "Integrated GPU",
                "Discrete GPU",
                "Virtual GPU",
                "CPU"
            };
            std::cout << typeStrings[props.deviceType] << ": " << props.deviceName << std::endl;
        }
    
        for (const auto& device : physicalDevices) {
            VkPhysicalDeviceFeatures supportedFeatures;
            supportedFeatures.samplerAnisotropy = VK_TRUE;
            vkGetPhysicalDeviceFeatures(device, &supportedFeatures);
            if (!supportedFeatures.samplerAnisotropy) {
                std::cout << "Sampler anisotropy is not supported!" << std::endl;
            }
            physicalDevice = device;
            break;
        }
    
        if (physicalDevice == VK_NULL_HANDLE) {
            throw std::runtime_error("Failed to create physical device!");
        }
    
        vkGetPhysicalDeviceProperties(physicalDevice, &physicalDeviceProperties);
    
        graphicsQueueFamilyIndex = UINT32_MAX;
        {
            uint32_t queueFamilyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
            std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());
    
            for (size_t i = 0; i < queueFamilies.size(); i++) {
                const auto& queueFamily = queueFamilies[i];
    
                if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                    graphicsQueueFamilyIndex = i;
                    break;
                }
            }
    
            if (graphicsQueueFamilyIndex == UINT32_MAX) {
                throw std::runtime_error("Failed to find a graphics queue family!");
            }
    
            VkDeviceQueueCreateInfo queueCreateInfo{};
            queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo.queueFamilyIndex = graphicsQueueFamilyIndex;
            queueCreateInfo.queueCount = 1;
    
            float queuePriority = 1.0f;
            queueCreateInfo.pQueuePriorities = &queuePriority;
    
            std::vector deviceExtensions = {
                VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                "VK_KHR_portability_subset",
                // VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME,
            };
            VkPhysicalDeviceFeatures deviceFeatures{};
            deviceFeatures.samplerAnisotropy = VK_TRUE;
            VkDeviceCreateInfo deviceCreateInfo{};
            deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
            deviceCreateInfo.queueCreateInfoCount = 1;
            deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
            deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
            deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();
            deviceCreateInfo.pEnabledFeatures = &deviceFeatures;
            VkResult result = vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device);
            if (result != VK_SUCCESS) {
                throw std::runtime_error("Failed to create Vulkan device!");
            }
        }
    
        vkGetDeviceQueue(device, graphicsQueueFamilyIndex, 0, &graphicsQueue);
        if (graphicsQueue == VK_NULL_HANDLE) {
            throw std::runtime_error("Failed to get graphics queue!");
        }
    
        {
            VkCommandPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            poolInfo.queueFamilyIndex = graphicsQueueFamilyIndex;
            poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; // Enable reset command buffer
            if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create command pool!");
            }
        }
    }
};
