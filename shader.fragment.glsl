#version 450

layout(location = 0) in vec3 fragPosition;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragColor;
layout(location = 3) in vec2 fragUV;
layout(location = 4) in vec3 cameraPos;

layout(location = 0) out vec4 outColor;

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
    vec3 lightPos = vec3(0.0, 1.0, 2.0);
    vec3 lightDiffuseFactor = vec3(1.0, 0.9, 0.8);
    vec3 ambient = vec3(0.04, 0.04, 0.07);
    vec3 diffuseColor = vec3(texture(diffuseTexture, fragUV)) * diffuseFactor;

    vec3 N = normalize(fragNormal);
    vec3 L = normalize(lightPos - fragPosition);
    vec3 viewDir = normalize(cameraPos - fragPosition);
    vec3 H = normalize(L + viewDir);
    float NdotH = dot(N, H);
    float NdotL = dot(N, L);
	float specularIntensity = pow(max(NdotH, 0.0), specularHardness) * specularPower;
    vec3 lightDiffuse = lightDiffuseFactor * max(NdotL, 0.0);
    lightDiffuse = ambient + lightDiffuse * (1 - ambient);
    lightDiffuse += lightDiffuse * specularIntensity;
    outColor = vec4(diffuseColor * lightDiffuse + emitFactor, 1.0);
}
