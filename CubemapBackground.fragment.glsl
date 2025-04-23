#version 450

layout(location = 0) in vec3 inTexCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 2) uniform samplerCube env;

void main() {
    outColor = texture(env, inTexCoord);
}
