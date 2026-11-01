#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
#include "skin.glsl"

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec3 vPosWS;
layout(location = 2) out vec3 vNormalWS;
layout(location = 3) out vec3 vTangentWS;
layout(location = 4) out vec3 vBitangentWS;
layout(location = 5) out vec4 vShadowCoord;

#include "material_block.glsl"

void main() {
    mat4 s = skinMatrix();
    mat4 w = PC.model * s;
    vec4 posWS = w * vec4(morphedPos(), 1.0);
    vec3 n = normalize(mat3(w) * inNormal);
    // Layering of clothes over skin (limits skinning interpenetration): push the cloth out along its normal and push
    // inner layers away from the camera along the view ray (same screen position, larger depth).
    posWS.xyz += n * M.m_layerOffset;
    posWS.xyz += normalize(posWS.xyz - G.cameraPos.xyz) * M.m_depthPush;
    vec3 t = normalize(mat3(w) * inTangent.xyz);
    vec3 b = cross(n, t) * inTangent.w;
    vPosWS = posWS.xyz;
    vNormalWS = n; vTangentWS = t; vBitangentWS = b;
    vUV = inUV * M.m_baseTexST.xy + M.m_baseTexST.zw;
    vShadowCoord = G.lightViewProj * posWS;
    gl_Position = G.viewProj * posWS;
}
