#pragma once

#include <vector>
#include <fstream>
#include <string>
#include <fkYAML.hpp>

std::vector<char> loadFile(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open file");
    }

    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return buffer;
}

std::string loadFileAsString(const char* filePath) {
    auto bytes = loadFile(filePath);
    return std::string(bytes.data(), bytes.size());
}

fkyaml::node loadYaml(const char* filePath) {
    std::string yamlContents = loadFileAsString(filePath);
    return fkyaml::node::deserialize(yamlContents);
}
