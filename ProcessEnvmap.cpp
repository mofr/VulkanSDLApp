#include <CLI11.hpp>
#include "CubemapFunctions.h"

int main(int argc, char** argv) {
    CLI::App app{ "Create 6 cubemap file from a single EXR equirectangular panorama" };

    std::string inputFilename;
    app.add_option("input", inputFilename, "Input EXR file")->required();

    std::string outputDir;
    app.add_option("output", outputDir, "Output directory")->required();

    int faceSize;
    app.add_option("-s,--size", faceSize, "Output face size")->default_val(256);

    CLI11_PARSE(app, argc, argv);

    ImageData imageData = loadImage(inputFilename);
    return convertEquirectangularToCubemap(imageData, outputDir.c_str(), faceSize);
}
