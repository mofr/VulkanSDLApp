#include <filesystem>
#include <fstream>
#include <vector>
#include <fkYAML.hpp>
#include "CubemapFunctions.h"

std::string loadFile(const char* filename) {
    std::ifstream ifs(filename, std::ios::in | std::ios::binary | std::ios::ate);

    std::ifstream::pos_type fileSize = ifs.tellg();
    ifs.seekg(0, std::ios::beg);

    std::vector<char> bytes(fileSize);
    ifs.read(bytes.data(), fileSize);

    return std::string(bytes.data(), fileSize);
}

int processEnvmap(const std::filesystem::path& assetPath, fkyaml::node const& yaml, const std::string& outDir) {
    int faceSize = yaml["faceSize"].as_int();
    bool saveAsKtx = yaml.contains("ktx") ? yaml["ktx"].as_bool() : false;
    const std::filesystem::path inputFileName = assetPath.string().substr(0, assetPath.string().size() - std::string(".asset.yaml").size());
    if (saveAsKtx) {
        std::string outputFileName = std::string(outDir / inputFileName.stem()) + ".ktx2";
        return convertEquirectangularToCubemapKtx(inputFileName.c_str(), outputFileName.c_str(), faceSize);
    } else {
        std::string outputDir = outDir / inputFileName.stem();
        return convertEquirectangularToCubemap(inputFileName.c_str(), outputDir.c_str(), faceSize);
    }
}

int processAsset(const std::filesystem::path& assetPath, const std::string& outDir) {
    std::string yamlContents = loadFile(assetPath.c_str());
    auto assetYaml = fkyaml::node::deserialize(yamlContents);
    std::string assetType = assetYaml["type"].as_str();

    if (assetType == "envmap") {
        return processEnvmap(assetPath, assetYaml, outDir);
    } else {
        std::cout << "Unknown asset type: " << assetType << std::endl;
        return -1;
    }
}

int main() {
    std::string assetsDir = "assets";
    std::string outDir = "build";
    int failureCount = 0;

    for (auto const& dirEntry : std::filesystem::recursive_directory_iterator(assetsDir)) {
        std::filesystem::path filePath = dirEntry.path();
        if (filePath.string().ends_with(".asset.yaml")) {
            std::cout << filePath.string();
            if (processAsset(filePath, outDir) != 0) {
                failureCount++;
                std::cout << " FAILED";
            }
            std::cout << "\n";
        }
    }
    if (failureCount) {
        std::cerr << "Failures: " << failureCount << std::endl;
    } else {
        std::cout << "No failures" << std::endl;
    }
    return failureCount == 0 ? 0 : 1;
}
