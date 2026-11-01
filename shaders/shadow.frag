#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
#include "material_block.glsl"
layout(set = 1, binding = 1) uniform sampler2D tBase;
layout(location = 0) in vec2 vUV;
void main() {
    float a = texture(tBase, vUV).a * M.m_baseColor.a * M.m_alpha;
    if (a - M.m_alphaClip < 0.0) discard;
}
