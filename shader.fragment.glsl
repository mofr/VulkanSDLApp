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
layout(set = 0, binding = 4) uniform Sun {
    vec3 sunDir;
    float _sunPadding1;
    vec3 sunRadiance;
    float _sunPadding2;
    float sunSolidAngle;
    vec3 _sunPadding3;
};
layout(set = 0, binding = 5) uniform sampler2D dfgLut;

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

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
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
    vec3 baseColor = texture(baseColorTexture, fragUV).rgb * baseColorFactor;
    float roughness = texture(roughnessTexture, fragUV).r * roughnessFactor;
    float metallic = metallicFactor;

    vec3 F0 = mix(vec3(0.04), baseColor, metallic);
    vec3 albedo = baseColor * (1.0 - metallic);

    vec3 N = normalize(fragNormal);
    vec3 V = normalize(cameraPos - fragPosition);
    float NdotV = max(dot(N, V), 0.0);
    vec3 R = reflect(-V, N);

    vec3 result = vec3(0);

    // Direct sun light
    if (sunRadiance.r > 0) {
        vec3 L = -sunDir;
        vec3 H = normalize(L + V);
        float NdotL = max(dot(N, L), 0.0);
        float HdotV = max(dot(H, V), 0.0);

        // Cook-Torrance BRDF
        float NDF = distributionGGX(N, H, roughness);
        float G = geometrySmith(N, V, L, roughness);
        vec3 F = fresnelSchlick(HdotV, F0);

        // Specular component
        vec3 specular = NDF * G * F / (4.0 * NdotV * NdotL + 0.001);

        // Diffuse component
        vec3 kD = 1.0 - F;
        vec3 diffuse = kD * (albedo / 3.14159) * NdotL;

        // Combine with light
        vec3 sunIrradiance = sunRadiance * sunSolidAngle;
        result += (diffuse + specular) * sunIrradiance * NdotL;
    }

    // Ambient
    {
        // Split diffuse and specular components based on fresnel factor
        vec3 ambient_kS = fresnelSchlickRoughness(NdotV, F0, roughness);
        vec3 ambient_kD = 1.0 - ambient_kS;

        // Diffuse component
        vec3 diffusedRadiance = lambertianReflectedRadiance(N);
        result += ambient_kD * diffusedRadiance * albedo;

        // Specular component
        float mipLevel = roughness * float(textureQueryLevels(env) - 1);
        vec3 prefilteredColor = textureLod(env, R, mipLevel).rgb;
        vec2 dfg = texture(dfgLut, vec2(NdotV, roughness)).rg;
        float scale = dfg.r;
        float bias = dfg.g;
        vec3 specularIBL = prefilteredColor * (scale * F0 + bias);
        result += ambient_kS * specularIBL;
    }

    result += emitFactor;

    outColor = vec4(result, 1.0);
}
