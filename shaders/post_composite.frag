#version 450
// Final composite: scene + bloom -> exposure -> tonemap -> split-tone / saturation / contrast -> vignette -> sRGB OETF (+dither).
layout(set = 0, binding = 0) uniform sampler2D tScene;
layout(set = 0, binding = 1) uniform sampler2D tBloom;
layout(set = 0, binding = 2) uniform sampler2D tLut;     // 3D colour LUT stored as a strip: x = blue slice * N + red, y = green
layout(set = 0, binding = 3) uniform sampler2D tHalation; // quarter-resolution HDR diffusion
layout(push_constant) uniform PC { vec4 a, b, c, d, e; } pc;   // e = LUT strength, LUT size, halation intensity
// a = bloom intensity, exposure, tonemap mode, vignette;  b = saturation, contrast, grain, vignette softness;  c = shadow tint.rgb, time;  d = highlight tint.rgb, aspect
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

vec3 neutralTonemap(vec3 color) {            // Khronos PBR Neutral: identity below 0.76, compresses highlights while keeping hue
    const float startCompression = 0.8 - 0.04, desaturation = 0.15;
    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;
    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression) return color;
    float d = 1.0 - startCompression;
    float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;
    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return mix(color, newPeak * vec3(1.0), g);
}
vec3 acesFilm(vec3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }
vec3 applyLut(vec3 c) {
    float N = pc.e.y;
    c = clamp(c, 0.0, 1.0);
    float b = c.b * (N - 1.0), b0 = floor(b), b1 = min(b0 + 1.0, N - 1.0), t = b - b0;
    float v = (c.g * (N - 1.0) + 0.5) / N;
    vec2 uv0 = vec2((b0 * N + c.r * (N - 1.0) + 0.5) / (N * N), v), uv1 = vec2((b1 * N + c.r * (N - 1.0) + 0.5) / (N * N), v);
    return mix(textureLod(tLut, uv0, 0.0).rgb, textureLod(tLut, uv1, 0.0).rgb, t);
}
float hash(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }

void main() {
    vec3 hdr = textureLod(tScene, vUV, 0.0).rgb;
    vec3 bloom = textureLod(tBloom, vUV, 0.0).rgb;
    hdr += bloom * pc.a.x * vec3(1.0, 0.97, 0.93);
    if (pc.e.z > 0.0) hdr += textureLod(tHalation, vUV, 0.0).rgb * pc.e.z * vec3(1.0, 0.32, 0.12);
    // Stylised ghost reflections from the already filtered HDR highlight buffer, not an occluded lens simulation.
    if (pc.e.w > 0.0) {
        vec2 centered = vUV - 0.5;
        vec3 g1 = textureLod(tBloom, 0.5 - centered * 0.65, 0.0).rgb;
        vec3 g2 = textureLod(tBloom, 0.5 + centered * 0.30, 0.0).rgb;
        float fade = 1.0 - smoothstep(0.35, 0.70, length(centered));
        hdr += (g1 * vec3(0.55, 0.72, 1.0) + g2 * vec3(1.0, 0.65, 0.40) * 0.5) * pc.e.w * fade;
    }
    hdr *= pc.a.y;
    int mode = int(pc.a.z + 0.5);
    vec3 c = mode == 1 ? neutralTonemap(hdr) : (mode == 2 ? acesFilm(hdr) : clamp(hdr, 0.0, 1.0));
    c = clamp(c, 0.0, 1.0);
    // grade in linear light: saturation + split toning
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = mix(vec3(l), c, pc.b.x);
    c *= mix(pc.c.rgb, pc.d.rgb, smoothstep(0.1, 0.8, l));
    c = clamp(c, 0.0, 1.0);
    c = mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
    c = (c - 0.5) * pc.b.y + 0.5;                                   // contrast around mid grey in display space
    c = clamp(c, 0.0, 1.0);
    if (pc.e.x > 0.001) c = mix(c, applyLut(c), pc.e.x);              // colour grading LUT (display referred)
    float r = length((vUV - 0.5) * vec2(min(pc.d.w, 1.5), 1.0));
    c *= 1.0 - pc.a.w * smoothstep(0.32, 0.32 + pc.b.w, r);
    c += (hash(gl_FragCoord.xy + fract(pc.c.w) * 71.0) - 0.5) * (pc.b.z + 1.0 / 255.0);
    outColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
