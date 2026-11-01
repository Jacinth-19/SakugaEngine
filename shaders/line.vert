#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inColor;
layout(location = 0) out vec4 vColor;
void main() { vColor = inColor; gl_Position = G.viewProj * vec4(inPos, 1.0); }
