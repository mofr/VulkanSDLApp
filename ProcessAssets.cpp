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
    std::filesystem::path inputFilename = assetPath.string().substr(0, assetPath.string().size() - std::string(".asset.yaml").size());
    std::string outputDir = outDir / inputFilename.stem();
    return convertEquirectangularToCubemap(inputFilename.c_str(), outputDir.c_str(), faceSize);
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

    for (auto const& dirEntry : std::filesystem::recursive_directory_iterator(assetsDir)) {
        std::filesystem::path filePath = dirEntry.path();
        if (filePath.string().ends_with(".asset.yaml")) {
            std::cout << filePath.string() << '\n';
            processAsset(filePath, outDir);
        }
    }
    return 0;
}
