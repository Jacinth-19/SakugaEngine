#version 450
#extension GL_GOOGLE_include_directive : require
#include "post_atmosphere_math.glsl"
layout(set = 0, binding = 0) uniform sampler2D tScene;
layout(set = 0, binding = 1) uniform sampler2D tDepth;
layout(push_constant) uniform PC { vec4 a, b, c, d, e; } pc; // a projection scalars, b fog RGB/density
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;
void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec3 scene = texelFetch(tScene, pixel, 0).rgb;
    float fog = fogForDepth(texelFetch(tDepth, pixel, 0).r, vUV, pc.a, pc.b.w);
    outColor = vec4(mix(scene, pc.b.rgb, clamp(fog, 0.0, 1.0)), 1.0);
}
