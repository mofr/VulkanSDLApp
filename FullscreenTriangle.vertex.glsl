#version 450

vec4 vertices[3] = vec4[3](
    vec4(-1.0, -1.0, 0.0, 1.0),
    vec4( 3.0, -1.0, 0.0, 1.0),
    vec4(-1.0,  3.0, 0.0, 1.0)
);

void main() {
    gl_Position = vertices[gl_VertexIndex];
}
