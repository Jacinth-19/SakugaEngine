#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
#include "skin.glsl"
#include "material_block.glsl"
layout(location = 0) out vec3 vPosWS;
void main() {
    mat4 w = PC.model * skinMatrix();
    vec3 posWS = (w * vec4(morphedPos(), 1.0)).xyz;
    vec3 nWS = normalize(mat3(w) * inNormal);
    // Inverted-hull outline. Width is in world units and grows gently with distance so it stays readable.
    float dist = length(G.cameraPos.xyz - posWS);
    float width = M.m_outlineWidth * G.split.w * mix(1.0, clamp(dist * 0.35, 0.6, 3.0), 0.75);
    posWS += nWS * (width + M.m_layerOffset);
    posWS += normalize(posWS - G.cameraPos.xyz) * M.m_depthPush;
    vPosWS = posWS;
    gl_Position = G.viewProj * vec4(posWS, 1.0);
}
