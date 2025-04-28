#pragma once

#include <glm/glm.hpp>

glm::vec3 temperatureToRgb(int temperature) {
    // Kelvin Temperature to RGB | Credit to Tanner Helland for the base algorithm
    // temperature is in Kelvin valid in the range 1000 K to 40000 K. White light = 6500K

    float t100 = temperature / 100; // Temperature divided by 100 for calculations
    float r, g, b;

    // RED
    if (t100 <= 66) {
        r = 255;
    } else {
        r = 329.698727446 * pow(t100 - 60, -0.1332047592);
    }
    if (r < 0) r = 0;
    if (r > 255) r = 255;

    // GREEN
    if (t100 <= 66) {
        g = 99.4708025861 * log(t100) - 161.1195681661;
    }
    else {
        g = 288.1221695283 * pow(t100 - 60, -0.0755148492);
    }
    if (g < 0) g = 0;
    if (g > 255) g = 255;

    // BLUE
    if (t100 >= 66) {
        b = 255;
    }
    else {
        if (t100 <= 19) {
            b = 0;
        } else {
            b = 138.5177312231 * log(t100 - 10) - 305.0447927307;
        }
    }
    if (b < 0) b = 0;
    if (b > 255) b = 255;

    return {r / 255, g / 255, b / 255};
}
