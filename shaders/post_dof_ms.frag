#version 450
#extension GL_GOOGLE_include_directive : require
#include "post_dof_common.glsl"
layout(set = 0, binding = 0) uniform sampler2D tScene;
layout(set = 0, binding = 1) uniform sampler2DMS tDepth;
layout(push_constant) uniform PC { vec4 a, b, c, d, e; } pc;
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;
float readDofDepth(vec2 uv) {
    ivec2 size = textureSize(tDepth);
    ivec2 px = clamp(ivec2(uv * vec2(size)), ivec2(0), size - 1);
    float depth = 1.0;
    for (int i = 0; i < int(pc.b.w); ++i) depth = min(depth, texelFetch(tDepth, px, i).r);
    return depth;
}
void main() {
    float depth = readDofDepth(vUV);
    outColor = vec4(blurDof(tScene, vUV, depth, pc.a, dofRadius(depth, pc.a, pc.b.x), pc.b.yz), 1.0);
}
