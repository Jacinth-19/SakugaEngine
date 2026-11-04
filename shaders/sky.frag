#version 450
#extension GL_GOOGLE_include_directive : require
// Stylised anime sky: time-of-day gradient driven by the C++ side, sun/moon disc with halo, cel-shaded cumulus
// (two light bands + bright rim facing the sun), thin cirrus wisps and stars at night.
#include "common.glsl"
layout(set = 1, binding = 1) uniform sampler2D uSkyTransmittance;
layout(location = 0) in vec3 vDir;
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outBloomMask;

float hash12(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1, 0)), f.x), mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), f.x), f.y);
}
float fbm(vec2 p) {
    float a = 0.5, s = 0.0;
    for (int i = 0; i < 5; ++i) { s += a * vnoise(p); p = p * 2.03 + vec2(17.1, 9.2); a *= 0.5; }
    return s;
}

float hash13(vec3 p) { p = fract(p * 0.1031); p += dot(p, p.zyx + 31.32); return fract((p.x + p.y) * p.z); }
float vnoise3(vec3 p) {
    vec3 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);
    float a = mix(mix(hash13(i), hash13(i + vec3(1, 0, 0)), f.x), mix(hash13(i + vec3(0, 1, 0)), hash13(i + vec3(1, 1, 0)), f.x), f.y);
    float b = mix(mix(hash13(i + vec3(0, 0, 1)), hash13(i + vec3(1, 0, 1)), f.x), mix(hash13(i + vec3(0, 1, 1)), hash13(i + vec3(1, 1, 1)), f.x), f.y);
    return mix(a, b, f.z);
}
float fbm3(vec3 p, int oct) { float a = 0.5, s = 0.0; for (int i = 0; i < oct; ++i) { s += a * vnoise3(p); p = p * 2.03 + vec3(11.7, 5.3, 17.1); a *= 0.5; } return s; }
float cloudField(vec3 d, float time) {          // seamless over the whole dome; flattened vertically so cumulus read as horizontal banks
    vec3 P = vec3(d.x, d.y * 1.7, d.z) * 3.0 + vec3(time * 0.010, 0.0, time * 0.004);
    return fbm3(P, 4) * 0.78 + fbm3(P * 2.9 + 3.1, 2) * 0.22 * 1.0;
}

// Texel-centre mapping must match the CPU LUT's linear mu/altitude grid.
vec4 atmosphereT(float mu, float altitudeKm) {
    vec2 ij = vec2(clamp(mu, 0.0, 1.0) * 255.0,
                   clamp(altitudeKm / 100.0, 0.0, 1.0) * 63.0);
    return textureLod(uSkyTransmittance, (ij + 0.5) / vec2(256.0, 64.0), 0.0);
}

void main() {
    outBloomMask = vec4(clamp(G.style.y, 0.0, 1.0), 0.0, 0.0, 1.0);
    vec3 d = normalize(vDir);
    if (G.envParams.z > 0.5) { outColor = vec4(encodeOutput(textureLod(uEnv, equirectUV(envRotate(d)), 0.0).rgb * G.envParams.x), 1.0); return; }

    vec3 L = normalize(G.lightDir.xyz), sunCol = G.lightColor.rgb;
    float h = clamp(d.y, 0.0, 1.0), night = G.skyParams.x, time = G.skyHorizon.w, cover = G.skyZenith.w;
    vec3 zenith = G.skyZenith.rgb, horizon = G.skyHorizon.rgb;
    float sd = max(dot(d, L), 0.0);

    // --- gradient + atmospheric glow towards the sun
    // three-stop gradient (saturated mid band) so the horizon is not a flat grey haze; slight saturation boost for the anime look
    vec3 mid = mix(horizon, zenith, 0.55); { float l = dot(mid, vec3(0.2126, 0.7152, 0.0722)); mid = max(mix(vec3(l), mid, 1.35), vec3(0.0)); }
    vec3 c = mix(horizon, mid, smoothstep(0.0, 0.22, h));
    c = mix(c, zenith, smoothstep(0.16, 0.85, h));
    c += horizon * 0.22 * (1.0 - smoothstep(0.0, 0.07, h)) * (1.0 - night);               // thin bright haze line on the horizon
    c += sunCol * (pow(sd, 6.0) * 0.22 + pow(sd, 28.0) * 0.28) * (1.0 - 0.6 * h) * (1.0 - night * 0.7);
    // Warm atmospheric scattering at the horizon when the sun is low, restrained at noon.
    float lowSun = 1.0 - smoothstep(0.08, 0.50, L.y);
    float horizonGlow = exp(-h * 7.0) * pow(sd, 3.0) * lowSun * (1.0 - night);
    c += sunCol * vec3(1.0, 0.62, 0.44) * horizonGlow * 0.20;
    // Approximate single scattering over the existing art-directed palette. The LUT stores
    // integrated view and sun transmittance; this is not a full per-sample path integral.
    // Virtual observer is fixed at sea level (0 km), independent of world camera height.
    if (G.skyParams.w > 0.0 && d.y >= 0.0 && night < 1.0) {
        vec4 viewT = atmosphereT(d.y, 0.0);
        vec3 sunT = atmosphereT(max(L.y, 0.0), 0.0).rgb;
        vec3 rayT = clamp(viewT.rgb / max(viewT.a, 0.0039), 0.0, 1.0);
        float cosTheta = clamp(dot(d, L), -1.0, 1.0);
        float rayPhase = 0.75 * (1.0 + cosTheta * cosTheta);
        const float asymmetry = 0.76;
        float miePhase = (1.0 - asymmetry * asymmetry) /
            pow(max(1.0 + asymmetry * asymmetry - 2.0 * asymmetry * cosTheta, 0.02), 1.5);
        vec3 scatter = (1.0 - rayT) * rayPhase * G.style.z +
                       vec3(1.0 - viewT.a) * min(miePhase, 6.0) * 0.15 * G.style.w;
        c += sunCol * sunT * scatter * G.skyParams.w * (1.0 - night);
    }
    c = mix(c, mix(horizon, G.groundColor.rgb, 0.6), (1.0 - smoothstep(-0.18, 0.0, d.y)));

    // --- stars
    if (night > 0.01 && d.y > 0.0) {
        vec2 sp = vec2(atan(d.z, d.x) * 90.0, asin(clamp(d.y, -1.0, 1.0)) * 180.0 / 1.5707963 * 1.0);
        vec2 cell = floor(sp); float r = hash12(cell);
        vec2 fp = fract(sp) - 0.5 - (vec2(hash12(cell + 7.7), hash12(cell + 3.1)) - 0.5) * 0.6;
        float star = step(0.985, r) * (1.0 - smoothstep(0.0, 0.28, length(fp))) * (0.5 + 0.5 * sin(time * (1.5 + r * 4.0) + r * 40.0));
        c += vec3(0.85, 0.9, 1.0) * star * night * smoothstep(0.02, 0.2, d.y) * 1.4;
    }

    // --- sun / moon disc (HDR value feeds the bloom pass)
    float size = 0.052 * G.skyParams.z;
    float disc = smoothstep(cos(size * 1.08), cos(size), dot(d, L));
    float ring = smoothstep(cos(size * 1.9), cos(size * 1.3), dot(d, L)) * (1.0 - disc);
    c = mix(c, sunCol * mix(4.0, 1.4, night) * G.skyParams.y, disc * step(0.0, d.y + 0.03));
    c += sunCol * ring * 0.35 * G.skyParams.y * step(0.0, d.y + 0.03);

    // --- cel-shaded cumulus on a projected plane: large puffy bodies, three light bands (shade / lit / sun-side highlight), silver lining
    float fade = smoothstep(0.012, 0.16, d.y) * smoothstep(0.0, 0.15, cover);
    vec3 dayLight = mix(vec3(1.0), sunCol, 0.45) * 1.06;
    vec3 shade = mix(horizon, zenith, 0.55) * 0.86 + vec3(0.07, 0.06, 0.14);              // violet-blue cloud shade (anime)
    vec3 shade2 = shade * vec3(0.78, 0.80, 0.98);                                          // deepest belly band
    if (fade > 0.0) {
        // far layer: a low stratocumulus bank hugging the horizon (gives depth)
        vec2 pb = d.xz / (d.y + 0.06) * vec2(0.16, 0.55) + vec2(time * 0.004, 0.0);
        float bank = smoothstep(0.50, 0.58, fbm(pb * 1.6 + 7.0)) * (1.0 - smoothstep(0.10, 0.34, d.y)) * smoothstep(0.012, 0.05, d.y);
        vec3 bankCol = mix(horizon * 0.92 + vec3(0.04, 0.03, 0.08), dayLight, 0.55 * (1.0 - night));
        c = mix(c, bankCol * mix(1.0, 0.2, night), bank * fade * 0.75);

        float n = cloudField(d, time);
        float thr = mix(0.70, 0.40, clamp(cover, 0.0, 1.0));
        float body = smoothstep(thr, thr + 0.03, n);
        float n2 = cloudField(normalize(d + L * 0.06 + vec3(0.0, 0.015, 0.0)), time);                // density towards the sun
        float g = n - n2;                                                                    // > 0: surface faces the sun
        float lit1 = smoothstep(-0.004, 0.012, g), lit2 = smoothstep(0.030, 0.050, g);
        float core = smoothstep(thr + 0.10, thr + 0.17, n);
        float deep = (1.0 - lit1) * core;
        vec3 cloud = mix(shade, dayLight * 0.97, lit1);                                      // band 1: shade -> lit
        cloud = mix(cloud, dayLight * 1.10 + sunCol * 0.10, lit2 * (1.0 - 0.35 * core));     // band 2: sun-side highlight
        cloud = mix(cloud, shade2, deep * 0.65);                                             // band 3: deep belly
        float edge = body * (1.0 - smoothstep(thr + 0.03, thr + 0.07, n));
        cloud += sunCol * edge * lit1 * 0.50 * (1.0 - night);                                // silver lining
        cloud *= mix(1.0, 0.16, night);
        c = mix(c, cloud, body * fade * 0.97);
        // high cirrus streaks
        vec2 q = d.xz / (d.y + 0.35) * vec2(0.35, 1.7) + vec2(time * 0.006, 0.0);
        float w = smoothstep(0.56, 0.82, fbm(q + 3.0 * fbm(q * 0.7)));
        c = mix(c, mix(dayLight, sunCol, 0.3) * mix(1.0, 0.2, night), w * 0.24 * fade * (0.4 + cover));
    }
    // --- distant ridgelines (3 layers of hills with atmospheric perspective): the far edge of the world reads as a landscape, not a flat horizon
    {
        float az = atan(d.z, d.x);
        vec3 hz = mix(horizon, vec3(dot(horizon, vec3(0.333))), 0.2);
        for (int i = 0; i < 3; ++i) {
            float fi = float(i);
            float hy = 0.006 + 0.022 * fi + (fbm(vec2(cos(az), sin(az)) * (2.4 + 1.1 * fi) + vec2(11.0 * fi, 3.0 * fi)) - 0.22) * (0.20 - 0.045 * fi);
            float m = (1.0 - smoothstep(hy - 0.002, hy + 0.003, d.y)) * smoothstep(-0.03, -0.004, d.y);
            vec3 hill = mix(hz, zenith * vec3(0.78, 0.74, 0.95) + vec3(0.03, 0.02, 0.08), 0.20 + 0.28 * fi);
            hill = mix(hill, hill * vec3(0.78, 0.80, 0.96), (1.0 - smoothstep(hy - 0.10, hy - 0.01, d.y)));          // darker towards the foot of each ridge
            c = mix(c, hill * mix(1.0, 0.22, night), m * 0.92);
        }
    }
    // --- god rays: angular streaks around the sun (day, low sun strongest), fading with distance from it
    {
        float ang = atan(dot(d, normalize(cross(L, vec3(0, 1, 0) + vec3(1e-3)))), dot(d, normalize(cross(cross(L, vec3(0, 1, 0) + vec3(1e-3)), L))));
        float streak = 0.5 + 0.5 * sin(ang * 11.0 + vnoise(vec2(ang * 3.0, time * 0.05)) * 6.0);
        float rayLowSun = 1.0 - smoothstep(0.1, 0.55, L.y);
        c += sunCol * pow(sd, 3.0) * streak * streak * (0.04 + 0.10 * rayLowSun) * (1.0 - night) * smoothstep(-0.05, 0.2, d.y);
    }
    outColor = vec4(encodeOutput(c), 1.0);
}
