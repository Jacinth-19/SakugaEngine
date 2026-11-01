#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(location = 0) in vec4 vColor;
layout(location = 0) out vec4 outColor;
void main() { outColor = vec4(encodeOutput(vColor.rgb), vColor.a); }
