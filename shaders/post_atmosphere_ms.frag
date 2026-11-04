#version 450
#extension GL_GOOGLE_include_directive : require
#include "post_atmosphere_math.glsl"
layout(set = 0, binding = 0) uniform sampler2D tScene;
layout(set = 0, binding = 1) uniform sampler2DMS tDepth;
layout(push_constant) uniform PC { vec4 a, b, c, d, e; } pc; // c.x depth sample count
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;
void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec3 scene = texelFetch(tScene, pixel, 0).rgb;
    float fog = 0.0;
    int samples = int(pc.c.x);
    for (int i = 0; i < samples; ++i) fog += fogForDepth(texelFetch(tDepth, pixel, i).r, vUV, pc.a, pc.b.w);
    fog /= float(samples);
    outColor = vec4(mix(scene, pc.b.rgb, clamp(fog, 0.0, 1.0)), 1.0);
}
