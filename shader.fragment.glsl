#version 450

layout(location = 0) in vec3 fragPosition; // Input from the vertex shader
layout(location = 1) in vec3 fragNormal; // Input from the vertex shader
layout(location = 2) in vec3 fragColor; // Input from the vertex shader

layout(location = 0) out vec4 outColor; // Output color to the framebuffer

void main() {
//    outColor = vec4(fragColor, 1.0);
//    return;

    vec3 lightPos = vec3(0.0, -3.0, -5.0); // Position of the light source
    float ambient = 0.15;

    vec3 normal = normalize(fragNormal);
//    outColor = vec4(normal * 0.5 + 0.5, 1.0);
//    return;

    // Calculate light direction
    vec3 lightDir = normalize(lightPos - fragPosition);

    // Calculate diffuse component
    float diff = max(dot(normal, lightDir), 0.0) * (1.0 - ambient) + ambient;

    // Simple color based on the lighting
    outColor = vec4(fragColor * diff, 1.0); // Apply lighting
}
