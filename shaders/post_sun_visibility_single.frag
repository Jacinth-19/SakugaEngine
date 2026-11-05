#version 450
#extension GL_GOOGLE_include_directive : require
layout(set = 0, binding = 0) uniform sampler2D tScene;
layout(set = 0, binding = 1) uniform sampler2D tDepth;
layout(push_constant) uniform PC { vec4 a, b, c, d, e, f, g, h; } pc;
layout(location = 0) out vec4 outColor;
float sunClearCoverage(vec2 uv) {
    ivec2 size=textureSize(tDepth,0);
    return float(texelFetch(tDepth,clamp(ivec2(uv*vec2(size)),ivec2(0),size-1),0).r >= 1.0);
}
#include "post_sun_visibility.glsl"
