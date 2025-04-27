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

void main() {
    vec3 N = normalize(fragNormal);
    vec3 ambient = vec3(0.0); // vec3(texture(env, N));
    vec3 diffuseColor = vec3(texture(diffuseTexture, fragUV)) * diffuseFactor;
    vec3 viewDir = normalize(cameraPos - fragPosition);

    vec3 lightDiffuse = vec3(0);
    for (int i = 0; i < lightCount; ++i) {
        vec3 lightPos = lights[i].pos;
        vec3 lightDiffuseFactor = lights[i].diffuseFactor;
        vec3 L = normalize(lightPos - fragPosition);
        vec3 H = normalize(L + viewDir);
        float NdotH = dot(N, H);
        float NdotL = dot(N, L);
        float specularIntensity = pow(max(NdotH, 0.0), specularHardness) * specularPower;
        vec3 diffuseContribution = lightDiffuseFactor * max(NdotL, 0.0);
        lightDiffuse += diffuseContribution + diffuseContribution * specularIntensity;
    }

    lightDiffuse += ambient;
    outColor = vec4(diffuseColor * lightDiffuse + emitFactor, 1.0);
}
