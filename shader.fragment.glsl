#version 450

layout(location = 0) in vec3 fragPosition;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragColor;
layout(location = 3) in vec2 fragUV;

layout(location = 0) out vec4 outColor; // Output color to the framebuffer

layout(binding = 1) uniform sampler2D texSampler;

void main() {
//    outColor = vec4(fragColor, 1.0);
//    return;

    vec3 lightPos = vec3(0.0, -3.0, -5.0); // Position of the light source
    vec3 ambient = vec3(0.02, 0.02, 0.1);

    vec3 normal = normalize(fragNormal);
//    outColor = vec4(normal * 0.5 + 0.5, 1.0);
//    return;

    // Calculate light direction
    vec3 lightDir = normalize(lightPos - fragPosition);

    // Calculate diffuse component
    float lightIntensity = max(dot(normal, lightDir), 0.0) * (1.0 - 0.1);

    vec4 textureColor = texture(texSampler, fragUV);
    vec3 diffuse = vec3(textureColor);

    // Simple color based on the lighting
    outColor = vec4(diffuse * lightIntensity + ambient, 1.0); // Apply lighting
}
