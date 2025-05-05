#version 450

layout(location = 0) in vec3 fragPosition;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragColor;
layout(location = 3) in vec2 fragUV;
layout(location = 4) in vec3 cameraPos;

layout(location = 0) out vec4 outColor;

struct Light {
    vec3 pos;
    float _padding1;
    vec3 diffuseFactor;
    float _padding2;
};

layout(set = 0, binding = 1) uniform LightBlock {
    Light lights[8];
    int lightCount;
};

layout(set = 0, binding = 2) uniform samplerCube env;

layout(set = 1, binding = 0) uniform sampler2D diffuseTexture;
layout(set = 1, binding = 1) uniform MaterialProps {
    vec3 diffuseFactor;
    float _padding1;
    vec3 emitFactor;
    float _padding2;
    float specularHardness;
    float specularPower;
};

layout(set = 0, binding = 3) uniform SphericalHarmonicsUBO {
    // 2nd order SH needs 9 coefficients per channel
    vec4 shCoefficients[9]; // 9 coefficients × 4 components (RGB + padding)
} sh;

vec3 reconstructIrradiance(vec3 normal) {
    // SH evaluation for 2nd order (L = 2) spherical harmonics
    const float c0 = 0.282095;
    const float c1 = 0.488603;
    const float c2 = 1.092548;
    const float c3 = 0.315392;
    const float c4 = 0.546274;
    
    // Constants for SH basis functions
    float x = normal.x;
    float y = normal.y;
    float z = normal.z;
    float x2 = x * x;
    float y2 = y * y;
    float z2 = z * z;
    
    // Evaluate spherical harmonics basis functions
    vec3 result = sh.shCoefficients[0].rgb * c0;       // L = 0, m = 0
    result += sh.shCoefficients[1].rgb * c1 * y;       // L = 1, m = -1
    result += sh.shCoefficients[2].rgb * c1 * z;       // L = 1, m = 0
    result += sh.shCoefficients[3].rgb * c1 * x;       // L = 1, m = 1
    result += sh.shCoefficients[4].rgb * c2 * (x * y); // L = 2, m = -2
    result += sh.shCoefficients[5].rgb * c2 * (y * z); // L = 2, m = -1
    result += sh.shCoefficients[6].rgb * c3 * (3 * z2 - 1.0); // L = 2, m = 0
    result += sh.shCoefficients[7].rgb * c2 * (x * z); // L = 2, m = 1
    result += sh.shCoefficients[8].rgb * c4 * (x2 - y2); // L = 2, m = 2
    
    return max(result, vec3(0.0));
}

void main() {
    vec3 N = normalize(fragNormal);
    vec3 ambient = reconstructIrradiance(N);
    vec3 materialDiffuse = vec3(texture(diffuseTexture, fragUV)) * diffuseFactor;
    vec3 viewDir = normalize(cameraPos - fragPosition);

    vec3 irradiance = vec3(0);
    for (int i = 0; i < lightCount; ++i) {
        vec3 lightPos = lights[i].pos;
        vec3 lightDiffuseFactor = lights[i].diffuseFactor;
        vec3 L = normalize(lightPos - fragPosition);
        vec3 H = normalize(L + viewDir);
        float NdotH = dot(N, H);
        float NdotL = dot(N, L);
        float specularIntensity = pow(max(NdotH, 0.0), specularHardness) * specularPower;
        vec3 diffuseContribution = lightDiffuseFactor * max(NdotL, 0.0);
        irradiance += diffuseContribution + diffuseContribution * specularIntensity;
    }

    irradiance += ambient;
    outColor = vec4(materialDiffuse * irradiance + emitFactor, 1.0);
}
