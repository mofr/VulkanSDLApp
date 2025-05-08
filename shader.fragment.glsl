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
    vec4 coeffs[9]; // vec4 = RGB + padding
} lambertianSH;

vec3 evaluateSH(vec3 dir, vec4 sh[9]) {
    // SH evaluation for 2nd order (L = 2) spherical harmonics

    float x = dir.x;
    float y = dir.y;
    float z = dir.z;
    float x2 = x * x;
    float y2 = y * y;
    float z2 = z * z;
    
    // Evaluate spherical harmonics basis functions
    vec3 result = vec3(0);
    result += sh[0].rgb * 0.282095;                  // L = 0, m = 0
    result += sh[1].rgb * 0.488603 * y;              // L = 1, m = -1
    result += sh[2].rgb * 0.488603 * z;              // L = 1, m = 0
    result += sh[3].rgb * 0.488603 * x;              // L = 1, m = 1
    result += sh[4].rgb * 1.092548 * (x * y);        // L = 2, m = -2
    result += sh[5].rgb * 1.092548 * (y * z);        // L = 2, m = -1
    result += sh[6].rgb * 0.315392 * (3 * z2 - 1.0); // L = 2, m = 0
    result += sh[7].rgb * 1.092548 * (x * z);        // L = 2, m = 1
    result += sh[8].rgb * 0.546274 * (x2 - y2);      // L = 2, m = 2
    
    return max(result, vec3(0.0));
}

vec3 lambertianReflectedRadiance(vec3 normal) {
    return evaluateSH(normal, lambertianSH.coeffs);
}

void main() {
    vec3 N = normalize(fragNormal);
    vec3 diffusedRadiance = lambertianReflectedRadiance(N);
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

    irradiance += diffusedRadiance;
    outColor = vec4(materialDiffuse * irradiance + emitFactor, 1.0);
}
