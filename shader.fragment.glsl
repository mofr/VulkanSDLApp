#version 450

layout(location = 0) in vec3 fragPosition;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragColor;
layout(location = 3) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D texSampler;

void main() {
//    outColor = texture(texSampler, fragUV);
//    return;

//    outColor = vec4(fragColor, 1.0);
//    return;

    vec3 lightPos = vec3(0.0, -3.0, -5.0);
    float ambient = 0.05;

    vec3 normal = normalize(fragNormal);
//    outColor = vec4(normal * 0.5 + 0.5, 1.0);
//    return;

    vec3 lightDir = normalize(lightPos - fragPosition);
    float lightIntensity = max(dot(normal, lightDir) + ambient, 0.0);

    vec3 textureColor = vec3(texture(texSampler, fragUV));

    outColor = vec4(textureColor * lightIntensity, 1.0);
}
