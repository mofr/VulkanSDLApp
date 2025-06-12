#pragma once

#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <iostream>
#include <ktx.h>
#include <vulkan/vulkan.h>

// Hammersley sequence generation for quasi-random sampling
float radicalInverse_VdC(uint32_t bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // = 1 / 0x100000000
}

// Returns Hammersley point for index i out of numSamples
std::pair<float, float> hammersley(uint32_t i, uint32_t numSamples) {
    return { float(i) / float(numSamples), radicalInverse_VdC(i) };
}

// GGX/Trowbridge-Reitz importance sampling
std::tuple<float, float, float> importanceSampleGGX(float u, float v, float roughness) {
    float a = roughness * roughness;
    
    // Sample in spherical coordinates
    float phi = 2.0f * M_PI * u;
    float cosTheta = std::sqrt((1.0f - v) / (1.0f + (a*a - 1.0f) * v));
    float sinTheta = std::sqrt(1.0f - cosTheta * cosTheta);
    
    // Convert to Cartesian coordinates (assuming N = (0,0,1))
    float x = sinTheta * std::cos(phi);
    float y = sinTheta * std::sin(phi);
    float z = cosTheta;
    
    return {x, y, z};
}

// Schlick's approximation for Fresnel
float schlickFresnel(float u) {
    return std::pow(1.0f - u, 5.0f);
}

// Smith's visibility function with GGX distribution
float geometrySchlickGGX(float NdotV, float roughness) {
    float a = roughness;
    float k = (a * a) / 8.0f;
    return NdotV / (NdotV * (1.0f - k) + k);
}

float geometrySmith(float NdotV, float NdotL, float roughness) {
    return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
}

// Calculate the BRDF scale and bias for a specific roughness and NdotV
std::pair<float, float> integrateBRDF(float NdotV, float roughness, uint32_t numSamples) {
    // View vector (in tangent space)
    float Vx = std::sqrt(1.0f - NdotV * NdotV); // sin
    float Vy = 0.0f;
    float Vz = NdotV; // cos
    
    float A = 0.0f; // Scale term
    float B = 0.0f; // Bias term
    
    for (uint32_t i = 0; i < numSamples; ++i) {
        // Get Hammersley sample point
        auto [u, v] = hammersley(i, numSamples);
        
        // Importance sample GGX Normal Distribution Function to get sample direction
        auto [Hx, Hy, Hz] = importanceSampleGGX(u, v, roughness);
        
        // Half vector
        float NdotH = Hz; // Since N = (0,0,1)
        
        // Calculate light vector by reflecting view vector around half vector
        float VdotH = Vx * Hx + Vy * Hy + Vz * Hz;
        
        // Calculate light vector via reflection
        float Lz = 2.0f * VdotH * Hz - Vz;
        
        float NdotL = Lz; // Since N = (0,0,1)
        
        // Skip samples that are not in the same hemisphere
        if (NdotL > 0.0f) {
            // Calculate geometry term
            float G = geometrySmith(NdotV, NdotL, roughness);
            
            // G_vis = G * VdotH / (NdotH * NdotV)
            // This is the GGX BRDF divided by its PDF
            float G_vis = G * VdotH / (NdotH * NdotV);
            
            // Fresnel term (simplified for integration)
            float Fc = schlickFresnel(VdotH);
            
            // Accumulate scale and bias
            A += (1.0f - Fc) * G_vis;
            B += Fc * G_vis;
        }
    }
    
    // Average over all samples
    A /= float(numSamples);
    B /= float(numSamples);
    
    return {A, B};
}

/**
 * Generates a DFG Lookup Table (LUT) for PBR rendering.
 * 
 * @param size Size of the LUT texture
 * @param numSamples Number of Monte Carlo samples per texel
 * @return Vector of floats containing the LUT data
 * 
 * The LUT maps roughness (y-axis) and NoV (x-axis) to scale and bias terms.
 * Values are stored as [r,g,r,g,...] where r=scale, g=bias
 */
std::vector<float> generateDFGLookupTable(
    uint32_t size, 
    uint32_t numSamples
) {
    std::vector<float> lutData(size * size * 2);
    
    // Generate LUT data
    #pragma omp parallel for collapse(2)
    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            // Convert texture coordinates to roughness and NdotV
            float roughness = float(y) / float(size - 1);
            float NdotV = float(x) / float(size - 1);

            // Use quadratic scale to increase sample count at grazing angles.
            NdotV *= NdotV;

            // Clamp NdotV to avoid singularity
            NdotV = std::max(NdotV, 0.001f);
            
            // Calculate BRDF terms
            auto [scale, bias] = integrateBRDF(NdotV, roughness, numSamples);

            // Store in the output array
            size_t index = (y * size + x) * 2;
            lutData[index + 0] = scale; // R = scale
            lutData[index + 1] = bias;  // G = bias
        }
    }
    
    return lutData;
}

int generate2DLookupTableToFile(std::vector<float> lutData, uint32_t size, const char* fileName) {
    ktxTexture2* texture;
    KTX_error_code result;
    
    ktxTextureCreateInfo createInfo = {
        .vkFormat = VK_FORMAT_R32G32_SFLOAT,
        .baseWidth = size,
        .baseHeight = size,
        .baseDepth = 1,
        .numDimensions = 2,
        .numLevels = 1,
        .numLayers = 1,
        .numFaces = 1,
        .isArray = false,
        .generateMipmaps = false,
    };
    result = ktxTexture2_Create(
        &createInfo,
        KTX_TEXTURE_CREATE_ALLOC_STORAGE,
        &texture
    );
    if (result != KTX_SUCCESS) {
        std::cerr << ktxErrorString(result) << std::endl;
        return -1;
    }
    
    ktx_uint32_t level = 0;
    ktx_uint32_t layer = 0;
    ktx_uint32_t faceSlice = 0;
    result = ktxTexture_SetImageFromMemory(
        ktxTexture(texture),
        level,
        layer,
        faceSlice,
        (ktx_uint8_t*)lutData.data(),
        lutData.size() * sizeof(lutData[0])
    );
    if (result != KTX_SUCCESS) {
        std::cerr << ktxErrorString(result) << std::endl;
        return -1;
    }

    ktxTexture_WriteToNamedFile(ktxTexture(texture), fileName);
    ktxTexture_Destroy(ktxTexture(texture));
    return 0;
}