#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
#include "skin.glsl"
#include "material_block.glsl"
layout(location = 0) out vec2 vUV;
void main() {
    mat4 w = PC.model * skinMatrix();
    vUV = inUV * M.m_baseTexST.xy + M.m_baseTexST.zw;
    mat4 lvp = PC.pad1 == 0u ? G.lightViewProj : (PC.pad1 == 1u ? G.lightViewProj1 : G.pointViewProj[PC.pad1 - 2u]);      // PC.pad1 = cascade index (0, 1) or 2 + slot * 6 + cube face in the shadow passes
    gl_Position = lvp * (w * vec4(morphedPos(), 1.0));
}
