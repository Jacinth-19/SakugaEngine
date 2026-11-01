#version 450
// Bloom prefilter: 4x4 box (Karis-weighted against fireflies) followed by a soft-knee brightness threshold.
layout(set = 0, binding = 0) uniform sampler2D tSrc;
layout(push_constant) uniform PC { vec4 a, b, c, d; } pc;   // a = threshold, knee, texel.xy
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;
float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
void main() {
    vec2 t = pc.a.zw;
    vec3 s0 = textureLod(tSrc, vUV + t * vec2(-1.0, -1.0), 0.0).rgb, s1 = textureLod(tSrc, vUV + t * vec2(1.0, -1.0), 0.0).rgb;
    vec3 s2 = textureLod(tSrc, vUV + t * vec2(-1.0, 1.0), 0.0).rgb,  s3 = textureLod(tSrc, vUV + t * vec2(1.0, 1.0), 0.0).rgb;
    float w0 = 1.0 / (1.0 + luma(s0)), w1 = 1.0 / (1.0 + luma(s1)), w2 = 1.0 / (1.0 + luma(s2)), w3 = 1.0 / (1.0 + luma(s3));
    vec3 c = (s0 * w0 + s1 * w1 + s2 * w2 + s3 * w3) / (w0 + w1 + w2 + w3);
    float thr = pc.a.x, knee = max(pc.a.y, 1e-4);
    float br = max(c.r, max(c.g, c.b));
    float soft = clamp(br - thr + knee, 0.0, 2.0 * knee); soft = soft * soft / (4.0 * knee);
    float contrib = max(soft, br - thr) / max(br, 1e-4);
    outColor = vec4(min(c * contrib, vec3(64.0)), 1.0);
}
