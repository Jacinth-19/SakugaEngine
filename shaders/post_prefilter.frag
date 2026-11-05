#version 450
// Bloom prefilter: 4x4 box (Karis-weighted against fireflies) followed by a soft-knee brightness threshold.
layout(set = 0, binding = 0) uniform sampler2D tSrc;
layout(set = 0, binding = 1) uniform sampler2D tMaterialMask;
layout(push_constant) uniform PC { vec4 a, b, c, d; } pc;   // a = threshold, knee, texel.xy
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;
float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
// Screen-space colour key: useful for art direction, not a per-material/object mask.
float selection(vec3 c) {
    c = max(c, vec3(0));
    float peak = max(c.r, max(c.g, c.b)), low = min(c.r, min(c.g, c.b));
    float warm = smoothstep(-.1, .3, (c.r-c.b)/max(peak,.001));
    int mode = int(pc.b.x + .5);
    if (mode == 1) return warm;
    if (mode == 2) return 1.0-warm;
    if (mode == 3) return smoothstep(.1, .45, (peak-low)/max(peak,.001));
    return 1.0;
}
vec3 highlight(vec3 c) {
    float thr = pc.a.x, knee = max(pc.a.y, 1e-4);
    float br = max(c.r, max(c.g, c.b));
    float soft = clamp(br - thr + knee, 0.0, 2.0 * knee); soft = soft * soft / (4.0 * knee);
    return max(c, vec3(0.0)) * (max(soft, br - thr) / max(br, 1e-4));
}
void main() {
    vec2 t = pc.a.zw;
    vec3 s0 = textureLod(tSrc, vUV + t * vec2(-1.0, -1.0), 0.0).rgb, s1 = textureLod(tSrc, vUV + t * vec2(1.0, -1.0), 0.0).rgb;
    vec3 s2 = textureLod(tSrc, vUV + t * vec2(-1.0, 1.0), 0.0).rgb,  s3 = textureLod(tSrc, vUV + t * vec2(1.0, 1.0), 0.0).rgb;
    float w0 = 1.0 / (1.0 + luma(s0)), w1 = 1.0 / (1.0 + luma(s1)), w2 = 1.0 / (1.0 + luma(s2)), w3 = 1.0 / (1.0 + luma(s3));
    // The material attachment contains selected HDR radiance, not a scalar
    // gate. This preserves emission colour and excludes reflected light.
    if (pc.b.y > 0.5) {
        s0 = textureLod(tMaterialMask, vUV + t * vec2(-1.0,-1.0), 0.0).rgb;
        s1 = textureLod(tMaterialMask, vUV + t * vec2( 1.0,-1.0), 0.0).rgb;
        s2 = textureLod(tMaterialMask, vUV + t * vec2(-1.0, 1.0), 0.0).rgb;
        s3 = textureLod(tMaterialMask, vUV + t * vec2( 1.0, 1.0), 0.0).rgb;
        w0 = 1.0 / (1.0 + luma(s0)); w1 = 1.0 / (1.0 + luma(s1));
        w2 = 1.0 / (1.0 + luma(s2)); w3 = 1.0 / (1.0 + luma(s3));
    }
    vec3 c = (highlight(s0) * selection(s0) * w0 +
              highlight(s1) * selection(s1) * w1 +
              highlight(s2) * selection(s2) * w2 +
              highlight(s3) * selection(s3) * w3) / (w0+w1+w2+w3);
    outColor = vec4(min(c, vec3(64.0)), 1.0);
}
