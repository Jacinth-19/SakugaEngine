#version 450
#extension GL_GOOGLE_include_directive : require
layout(set = 0, binding = 0) uniform sampler2D tDisplay;
layout(set = 0, binding = 1) uniform sampler2D tDepth;
layout(push_constant) uniform PC { vec4 a, b, c, d, e; } pc;
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;
float finishDepth(vec2 uv) {
    ivec2 size=textureSize(tDepth,0);
    return texelFetch(tDepth,clamp(ivec2(uv*vec2(size)),ivec2(0),size-1),0).r;
}
#include "post_finish_depth_common.glsl"
#define SAKUGA_DEPTH_INK
#include "post_finish_common.glsl"
