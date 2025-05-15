#include <filesystem>
#include <fstream>
#include <vector>
#include <format>
#include <fkYAML.hpp>
#include "FileFunctions.h"
#include "CubemapFunctions.h"
#include "SunExtraction.h"

void saveSunDataToFile(ExtractedSunData const& sunData, const char* fileName) {
    std::ofstream ofs(fileName);
    ofs << std::fixed << std::setprecision(6);

    ofs << "dir: [";
    ofs << sunData.dir.x << ", ";
    ofs << sunData.dir.y << ", ";
    ofs << sunData.dir.z << "]";
    ofs << std::endl;

    ofs << "radiance: [";
    ofs << sunData.radiance.r << ", ";
    ofs << sunData.radiance.g << ", ";
    ofs << sunData.radiance.b << "]";
    ofs << std::endl;

    ofs << "solidAngle: " << sunData.solidAngle;
}

int processEnvmap(const std::filesystem::path& assetPath, fkyaml::node const& yaml, const std::string& outDir) {
    int faceSize = yaml["faceSize"].as_int();
    bool saveAsKtx = yaml.contains("ktx") ? yaml["ktx"].as_bool() : false;
    bool extractSun = yaml.contains("extractSun") ? yaml["extractSun"].as_bool() : false;
    const std::filesystem::path inputFileName = assetPath.string().substr(0, assetPath.string().size() - std::string(".asset.yaml").size());
    ImageData imageData = loadImage(inputFileName);

    if (saveAsKtx) {
        std::string outputFileName = std::string(outDir / inputFileName.stem()) + ".ktx2";
        if (convertEquirectangularToCubemapKtx(imageData, outputFileName.c_str(), faceSize) != 0) {
            return -1;
        }
    } else {
        std::string outputDir = outDir / inputFileName.stem();
        if (convertEquirectangularToCubemap(imageData, outputDir.c_str(), faceSize) != 0) {
            return -1;
        }
    }

    if (extractSun) {
        float sunSolidAngle = yaml["sunSolidAngle"].as_float();
        ExtractedSunData sunData = extractSunFromEquirectangularPanorama(imageData, sunSolidAngle);
        if (sunData.error) {
            std::cout << "Failed to extract sun: " << sunData.error << std::endl;
            return -1;
        }
        std::string sunDataFileName = std::string(outDir / inputFileName.stem()) + ".sun.yaml";
        saveSunDataToFile(sunData, sunDataFileName.c_str());
    }

    std::string diffuseShFileName = std::string(outDir / inputFileName.stem()) + ".sh.txt";
    if (calculateDiffuseSphericalHarmonics(imageData, diffuseShFileName.c_str()) != 0) {
        return -1;
    }
    return 0;
}

int processAsset(const std::filesystem::path& assetPath, const std::string& outDir) {
    auto assetYaml = loadYaml(assetPath.c_str());
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
            std::cout << filePath.string() << std::endl;
            if (processAsset(filePath, outDir) != 0) {
                failureCount++;
                std::cout << " FAILED" << std::endl;
            }
        }
    }
    if (failureCount) {
        std::cerr << "Failures: " << failureCount << std::endl;
    } else {
        std::cout << "No failures" << std::endl;
    }
    return failureCount == 0 ? 0 : 1;
}
