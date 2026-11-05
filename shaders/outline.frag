#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
#include "material_block.glsl"
layout(location = 0) in vec3 vPosWS;
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outBloomMask;
void main() {
    if (M.m_outlineAlpha - 0.001 < 0.0) discard;
    float dist = length(G.cameraPos.xyz - vPosWS);
    float fog = 1.0 - exp2(-G.fogColor.w * G.fogColor.w * dist * dist * 1.442695);
    vec3 c = mix(M.m_outlineColor.rgb, G.fogColor.rgb, clamp(fog, 0.0, 1.0));
    outColor = vec4(encodeOutput(c), 1.0);
    outBloomMask = vec4(encodeOutput(c) * clamp(M.m_bloomWeight, 0.0, 1.0) * (1.0 - clamp(M.m_bloomEmissionOnly, 0.0, 1.0)), 1.0);
}
