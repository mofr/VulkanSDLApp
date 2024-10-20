#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inUV;

layout(location = 0) out vec3 fragPosition;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragColor;
layout(location = 3) out vec2 fragUV;

layout(set = 0, binding = 0) uniform Model {
    mat4 model;
};
layout(set = 1, binding = 0) uniform ViewProjection {
    mat4 view;
    mat4 projection;
};

void main() {
    fragPosition = vec3(model * vec4(inPosition, 1.0)); // Transform position
    fragNormal = mat3(transpose(inverse(model))) * inNormal; // Transform normal
    fragColor = inColor; // Pass the color to the fragment shader
    fragUV = inUV;

    gl_Position = projection * view * vec4(fragPosition, 1.0); // Clip space position
}
