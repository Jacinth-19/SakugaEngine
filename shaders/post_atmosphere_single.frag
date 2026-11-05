#version 450
#extension GL_GOOGLE_include_directive : require
#include "post_atmosphere_math.glsl"
layout(set = 0, binding = 0) uniform sampler2D tScene;
layout(set = 0, binding = 1) uniform sampler2D tDepth;
layout(set = 0, binding = 2) uniform sampler2D tTransmittance;
layout(push_constant) uniform PC { vec4 a, b, c, d, e, f, g, h; } pc;
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;
#include "post_aerial.glsl"
void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec3 scene = texelFetch(tScene, pixel, 0).rgb;
    float depth = texelFetch(tDepth, pixel, 0).r;
    if (pc.c.y > 0.5) outColor = vec4(aerialForDepth(scene, depth, vUV), 1.0);
    else {
        float fog = fogForDepth(depth, vUV, pc.a, pc.b.w);
        outColor = vec4(mix(scene, pc.b.rgb, clamp(fog, 0.0, 1.0)), 1.0);
    }
}
