#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(location = 0) out vec3 vDir;
void main() {
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vec2 ndc = p * 2.0 - 1.0;
    gl_Position = vec4(ndc, 1.0, 1.0);
    vec3 viewDir = vec3(ndc.x / G.proj[0][0], ndc.y / G.proj[1][1], -1.0);
    vDir = transpose(mat3(G.view)) * viewDir;
}
