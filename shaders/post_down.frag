#version 450
// 13-tap dual-filter style downsample (Call of Duty: Advanced Warfare), stable and flicker free.
layout(set = 0, binding = 0) uniform sampler2D tSrc;
layout(push_constant) uniform PC { vec4 a, b, c, d; } pc;   // a.xy = source texel
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;
void main() {
    vec2 t = pc.a.xy;
    #define S(o) textureLod(tSrc, vUV + (o) * t, 0.0).rgb
    vec3 a = S(vec2(-2, -2)), b = S(vec2(0, -2)), c = S(vec2(2, -2));
    vec3 d = S(vec2(-2, 0)),  e = S(vec2(0, 0)),  f = S(vec2(2, 0));
    vec3 g = S(vec2(-2, 2)),  h = S(vec2(0, 2)),  i = S(vec2(2, 2));
    vec3 j = S(vec2(-1, -1)), k = S(vec2(1, -1)), l = S(vec2(-1, 1)), m = S(vec2(1, 1));
    vec3 r = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
    outColor = vec4(r, 1.0);
}
