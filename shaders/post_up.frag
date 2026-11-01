#version 450
// 3x3 tent upsample; blended additively onto the next larger bloom level.
layout(set = 0, binding = 0) uniform sampler2D tSrc;
layout(push_constant) uniform PC { vec4 a, b, c, d; } pc;   // a.xy = source texel, a.z = radius
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;
void main() {
    vec2 t = pc.a.xy * pc.a.z;
    vec3 r = textureLod(tSrc, vUV + t * vec2(-1, -1), 0.0).rgb + 2.0 * textureLod(tSrc, vUV + t * vec2(0, -1), 0.0).rgb + textureLod(tSrc, vUV + t * vec2(1, -1), 0.0).rgb
           + 2.0 * textureLod(tSrc, vUV + t * vec2(-1, 0), 0.0).rgb + 4.0 * textureLod(tSrc, vUV, 0.0).rgb + 2.0 * textureLod(tSrc, vUV + t * vec2(1, 0), 0.0).rgb
           + textureLod(tSrc, vUV + t * vec2(-1, 1), 0.0).rgb + 2.0 * textureLod(tSrc, vUV + t * vec2(0, 1), 0.0).rgb + textureLod(tSrc, vUV + t * vec2(1, 1), 0.0).rgb;
    outColor = vec4(r * (1.0 / 16.0) * 0.8, 1.0);
}
