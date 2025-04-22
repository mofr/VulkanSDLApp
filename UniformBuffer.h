#pragma once

#include <vulkan/vulkan.h>

template<typename T>
class UniformBuffer {
public:
    UniformBuffer(VkPhysicalDevice physicalDevice, VkDevice device):
        m_device(device)
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size();
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device, &bufferInfo, nullptr, &m_buffer) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create uniform buffer!");
        }
        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(device, m_buffer, &memRequirements);
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(device, &allocInfo, nullptr, &m_bufferMemory) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate uniform buffer memory!");
        }
        vkBindBufferMemory(device, m_buffer, m_bufferMemory, 0);
        if (vkMapMemory(device, m_bufferMemory, 0, size(), 0, (void**)&m_mappedData) != VK_SUCCESS) {
            throw std::runtime_error("Failed to map uniform buffer memory!");
        }
    }

    ~UniformBuffer() {
        vkUnmapMemory(m_device, m_bufferMemory);
        vkFreeMemory(m_device, m_bufferMemory, nullptr);
        vkDestroyBuffer(m_device, m_buffer, nullptr);
    }

    VkBuffer buffer() const {return m_buffer;}
    VkDeviceSize size() const {return sizeof(T);}
    VkDescriptorBufferInfo descriptorBufferInfo() const {
        return {.buffer = m_buffer, .range = size()};
    };
    T& data() {return *m_mappedData;}

private:
    VkDevice m_device;
    VkBuffer m_buffer;
    VkDeviceMemory m_bufferMemory;
    T* m_mappedData;
};


template<typename T>
class UniformBuffer<T[]> {
public:
    UniformBuffer(VkPhysicalDevice physicalDevice, VkDevice device, uint32_t count):
        m_device(device), m_count(count)
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size();
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device, &bufferInfo, nullptr, &m_buffer) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create uniform buffer!");
        }
        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(device, m_buffer, &memRequirements);
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(device, &allocInfo, nullptr, &m_bufferMemory) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate uniform buffer memory!");
        }
        vkBindBufferMemory(device, m_buffer, m_bufferMemory, 0);
        if (vkMapMemory(device, m_bufferMemory, 0, size(), 0, (void**)&m_mappedData) != VK_SUCCESS) {
            throw std::runtime_error("Failed to map uniform buffer memory!");
        }
    }

    ~UniformBuffer() {
        vkUnmapMemory(m_device, m_bufferMemory);
        vkFreeMemory(m_device, m_bufferMemory, nullptr);
        vkDestroyBuffer(m_device, m_buffer, nullptr);
    }

    VkBuffer buffer() const {return m_buffer;}
    VkDeviceSize size() const {return sizeof(T) * m_count;}
    VkDescriptorBufferInfo descriptorBufferInfo() const {
        return {.buffer = m_buffer, .range = size()};
    };
    VkDescriptorBufferInfo descriptorBufferInfo(uint32_t elementIndex) const {
        return {.buffer = m_buffer, .range = sizeof(T), .offset = elementIndex * sizeof(T)};
    };
    std::span<T> data() {return {m_mappedData, m_count};}

private:
    VkDevice m_device;
    VkBuffer m_buffer;
    VkDeviceMemory m_bufferMemory;
    T* m_mappedData;
    uint32_t m_count;
};
