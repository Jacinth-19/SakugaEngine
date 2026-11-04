#version 450
// Quarter-resolution red-biased diffusion source. The HDR bloom pyramid has already selected bright pixels.
// A wider 9-tap tent broadens the glow independently of the normal bloom, before exposure/tonemapping.
layout(set = 0, binding = 0) uniform sampler2D tBright;
layout(push_constant) uniform PC { vec4 a, b, c, d, e; } pc; // a.xy = source texel, a.z = radius
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;
void main() {
    vec2 t = pc.a.xy * pc.a.z;
    vec3 c = textureLod(tBright, vUV, 0.0).rgb * 4.0;
    c += (textureLod(tBright, vUV + vec2(t.x, 0), 0.0).rgb + textureLod(tBright, vUV - vec2(t.x, 0), 0.0).rgb
        + textureLod(tBright, vUV + vec2(0, t.y), 0.0).rgb + textureLod(tBright, vUV - vec2(0, t.y), 0.0).rgb) * 2.0;
    c += textureLod(tBright, vUV + t, 0.0).rgb + textureLod(tBright, vUV - t, 0.0).rgb
       + textureLod(tBright, vUV + vec2(t.x, -t.y), 0.0).rgb + textureLod(tBright, vUV + vec2(-t.x, t.y), 0.0).rgb;
    outColor = vec4(c / 16.0, 1.0);
}
