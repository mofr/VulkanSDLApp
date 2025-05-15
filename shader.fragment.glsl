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

layout(set = 0, binding = 4) uniform Sun {
    vec3 sunDir;
    float _sunPadding1;
    vec3 sunRadiance;
    float _sunPadding2;
    float sunSolidAngle;
    vec3 _sunPadding3;
};

layout(set = 1, binding = 0) uniform sampler2D baseColorTexture;
layout(set = 1, binding = 1) uniform sampler2D roughnessTexture;
layout(set = 1, binding = 2) uniform MaterialProps {
    vec3 baseColorFactor;
    float _padding1;
    vec3 emitFactor;
    float _padding2;
    float roughnessFactor;
    float metallicFactor;
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

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / (3.14159 * denom * denom);
}

float geometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
}

void main() {
    vec3 albedo = texture(baseColorTexture, fragUV).rgb * baseColorFactor;
    float roughness = texture(roughnessTexture, fragUV).r * roughnessFactor;
    float metallic = metallicFactor;

    vec3 N = normalize(fragNormal);
    vec3 V = normalize(cameraPos - fragPosition);
    vec3 diffusedRadiance = lambertianReflectedRadiance(N);

    vec3 result = vec3(0);

    // PBR sun
    if (sunRadiance.r > 0) {
        vec3 L = -sunDir;
        vec3 H = normalize(L + V);

        // F0 reflectance
        vec3 F0 = mix(vec3(0.04), albedo, metallic);

        // Cook-Torrance BRDF
        float NDF = distributionGGX(N, H, roughness);
        float G = geometrySmith(N, V, L, roughness);
        vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 numerator = NDF * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.001;
        vec3 specular = numerator / denominator;

        // kS is energy of specular, kD is energy of diffuse
        vec3 kS = F;
        vec3 kD = vec3(1.0) - kS;
        kD *= 1.0 - metallic;

        float NdotL = max(dot(N, L), 0.0);
        vec3 diffuse = (albedo / 3.14159) * NdotL;

        vec3 sunIrradiance = sunRadiance * sunSolidAngle;

        result += (kD * diffuse + specular) * sunIrradiance * NdotL;
    }

    result += diffusedRadiance * albedo;
    result += emitFactor;

    // Old point lights. To be replaced with PBR model.
    for (int i = 0; i < lightCount; ++i) {
        vec3 lightPos = lights[i].pos;
        vec3 lightDiffuseFactor = lights[i].diffuseFactor;
        vec3 L = normalize(lightPos - fragPosition);
        vec3 H = normalize(L + V);
        float NdotH = dot(N, H);
        float NdotL = dot(N, L);
        // float specularIntensity = pow(max(NdotH, 0.0), specularHardness) * specularPower;
        // vec3 diffuseContribution = lightDiffuseFactor * max(NdotL, 0.0);
    }

    outColor = vec4(result, 1.0);
}
