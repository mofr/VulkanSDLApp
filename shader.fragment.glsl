#version 450

layout(location = 0) in vec3 fragPosition;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragColor;
layout(location = 3) in vec2 fragUV;
layout(location = 4) in vec3 cameraPos;

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D texSampler;

const float specularHardness = 500.0;
const float specularPower = 5.0;

void main() {
    vec3 lightPos = vec3(0.0, 3.0, 5.0);
    float ambient = 0.04;
    vec3 textureColor = vec3(texture(texSampler, fragUV));

    vec3 N = normalize(fragNormal);
    vec3 L = normalize(lightPos - fragPosition);
    vec3 viewDir = normalize(cameraPos - fragPosition);
    vec3 H = normalize(L + viewDir);
    float NdotH = dot(N, H);
    float NdotL = dot(N, L);
	float specularIntensity = pow(max(NdotH, 0.0), specularHardness) * specularPower;
    float diffuseIntensity = max(NdotL, 0.0);
    diffuseIntensity = ambient + diffuseIntensity * (1 - ambient);
    diffuseIntensity += diffuseIntensity * specularIntensity;
    outColor = vec4(textureColor * diffuseIntensity, 1.0);
}
