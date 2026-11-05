#version 450
#extension GL_GOOGLE_include_directive : require
layout(set = 0, binding = 0) uniform sampler2D tScene;
layout(set = 0, binding = 1) uniform sampler2DMS tDepth;
layout(push_constant) uniform PC { vec4 a, b, c, d, e, f, g, h; } pc;
layout(location = 0) out vec4 outColor;
float sunClearCoverage(vec2 uv) {
    ivec2 size=textureSize(tDepth), p=clamp(ivec2(uv*vec2(size)),ivec2(0),size-1);
    float coverage=0.0;
    for(int i=0;i<int(pc.g.x);++i) coverage+=float(texelFetch(tDepth,p,i).r>=1.0);
    return coverage / max(pc.g.x,1.0);
}
#include "post_sun_visibility.glsl"
