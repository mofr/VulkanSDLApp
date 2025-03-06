#version 450

layout(location = 0) in vec3 fragPosition;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragColor;
layout(location = 3) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D texSampler;

void main() {
    vec3 lightPos = vec3(0.0, -3.0, -5.0);
    float ambient = 0.04;
    vec3 textureColor = vec3(texture(texSampler, fragUV));
    textureColor = pow(textureColor, vec3(2.2)); // Convert to linear space from sRGB

    vec3 normal = normalize(fragNormal);
    vec3 lightDir = normalize(lightPos - fragPosition);
    float lightIntensity = max(dot(normal, lightDir), 0.0);
    lightIntensity = ambient + lightIntensity * (1 - ambient);
    vec3 resultColor = textureColor * lightIntensity;

    resultColor = pow(resultColor, vec3(1 / 2.2)); // Apply sRGB gamma correction

    outColor = vec4(resultColor, 1.0);
}
