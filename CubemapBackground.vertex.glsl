#version 450

layout(location = 0) out vec3 outTexCoord;

layout(set = 0, binding = 0) uniform ViewProjection {
    mat4 view;
    mat4 projection;
};

vec3 vertices[8] = vec3[8](
    vec3(-1.0, -1.0, -1.0),
    vec3( 1.0, -1.0, -1.0),
    vec3( 1.0,  1.0, -1.0),
    vec3(-1.0,  1.0, -1.0),
    vec3(-1.0, -1.0,  1.0),
    vec3( 1.0, -1.0,  1.0),
    vec3( 1.0,  1.0,  1.0),
    vec3(-1.0,  1.0,  1.0)
);

int indices[36] = int[36](
    // Front face
    0, 1, 2, 2, 3, 0,
    // Back face
    4, 7, 6, 6, 5, 4,
    // Left face
    0, 3, 7, 7, 4, 0,
    // Right face
    1, 5, 6, 6, 2, 1,
    // Bottom face
    0, 4, 5, 5, 1, 0,
    // Top face
    3, 2, 6, 6, 7, 3
);

void main() {
    vec3 position = vertices[indices[gl_VertexIndex]];
    outTexCoord = position;
    mat4 viewWithoutTranslation = view;
    viewWithoutTranslation[3] = vec4(0.0, 0.0, 0.0, 1.0);
    vec4 pos = projection * viewWithoutTranslation * vec4(position, 1.0);
    gl_Position = pos.xyww;
}
