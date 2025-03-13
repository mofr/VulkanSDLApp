#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inUV;

layout(location = 0) out vec3 fragPosition;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragColor;
layout(location = 3) out vec2 fragUV;
layout(location = 4) out vec3 cameraPos;

layout(set = 0, binding = 0) uniform ModelTransform {
    mat4 model;
};
layout(set = 2, binding = 0) uniform ViewProjection {
    mat4 view;
    mat4 projection;
};

void main() {
    fragPosition = vec3(model * vec4(inPosition, 1.0));
    fragNormal = mat3(transpose(inverse(model))) * inNormal;
    fragColor = inColor;
    fragUV = inUV;
    cameraPos = vec3(inverse(view) * vec4(0, 0, 0, 1));

    gl_Position = projection * view * vec4(fragPosition, 1.0); // Clip space position
}
