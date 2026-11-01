#version 450
#extension GL_GOOGLE_include_directive : require
// Sakuga/Hybrid -- GLSL port of Custom/EndfieldHybrid.shader (Unity URP HLSL).
// PBR base (URP-style simplified BRDF) hybridised with ramp-based NPR shading, face SDF shadows, anisotropic hair
// spec, matcap, rim, wet-surface raindrops, split-screen NPR comparison and debug views.
#include "common.glsl"
#include "material_block.glsl"

layout(set = 1, binding = 1)  uniform sampler2D tBase;
layout(set = 1, binding = 2)  uniform sampler2D tNormal;
layout(set = 1, binding = 3)  uniform sampler2D tParam;
layout(set = 1, binding = 4)  uniform sampler2D tStyle;
layout(set = 1, binding = 5)  uniform sampler2D tRamp;
layout(set = 1, binding = 6)  uniform sampler2D tSpecRamp;
layout(set = 1, binding = 7)  uniform sampler2D tLut;
layout(set = 1, binding = 8)  uniform sampler2D tMatCap;
layout(set = 1, binding = 9)  uniform sampler2D tMask;
layout(set = 1, binding = 10) uniform sampler2D tFaceSDF;
layout(set = 1, binding = 11) uniform sampler2D tHighlight;
layout(set = 1, binding = 12) uniform sampler2D tHairSpec;
layout(set = 1, binding = 13) uniform sampler2D tEmission;
layout(set = 1, binding = 14) uniform sampler2D tFlow;

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec3 vPosWS;
layout(location = 2) in vec3 vNormalWS;
layout(location = 3) in vec3 vTangentWS;
layout(location = 4) in vec3 vBitangentWS;
layout(location = 5) in vec4 vShadowCoord;
layout(location = 0) out vec4 outColor;

float saturate(float x) { return clamp(x, 0.0, 1.0); }
vec3  saturate(vec3 x)  { return clamp(x, 0.0, 1.0); }
float frac(float x) { return x - floor(x); }
vec2  frac(vec2 x)  { return x - floor(x); }

// ------------------------------------------------------------------ helpers (1:1 with the HLSL)
vec3 sampleLut(vec3 color) {
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    vec3 lut = texture(tLut, vec2(saturate(luma), M.m_lutRow)).rgb;
    return mix(color, color * lut, M.m_lutStrength);
}

float selectChannel(vec4 v, float channel) {
    float r = v.r;
    r = mix(r, v.g, step(0.5, channel) * (1.0 - step(1.5, channel)));
    r = mix(r, v.b, step(1.5, channel) * (1.0 - step(2.5, channel)));
    r = mix(r, v.a, step(2.5, channel));
    return r;
}

float hash21(vec2 p) {
    p = frac(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return frac(p.x * p.y);
}

float sawFade(float t, float fadeIn, float fadeOut) {
    return smoothstep(0.0, fadeIn, t) * (1.0 - smoothstep(1.0 - fadeOut, 1.0, t));
}

float rainLayer(vec2 uv, float scale, float speed, float density, float seed) {
    vec2 p = uv * scale;
    vec2 id = floor(p);
    vec2 gv = frac(p) - 0.5;
    float rnd = hash21(id + seed);
    float rainTime = G.misc.z > 0.0 ? G.misc.z : G.cameraPos.w;
    float phase = frac(rainTime * speed * 0.22 + rnd);
    float isActive = step(1.0 - saturate(density), rnd);
    float fade = sawFade(phase, 0.16, 0.34) * isActive;

    vec2 offset = vec2(hash21(id + seed + 2.17) - 0.5, hash21(id + seed + 5.93) - 0.5) * vec2(0.48, 0.18);
    offset.y += mix(0.24, -0.20, smoothstep(0.10, 0.92, phase));
    vec2 q = gv - offset;

    float radius = mix(0.055, 0.135, smoothstep(0.0, 0.34, phase));
    float d = length(q / vec2(0.92, 1.08));
    float body = 1.0 - smoothstep(radius * 0.55, radius, d);
    float edge = (1.0 - smoothstep(radius, radius + 0.045, d)) * smoothstep(radius * 0.48, radius * 0.82, d);
    float drop = saturate(body * 0.75 + edge * 0.45) * fade;

    float tailWindow = smoothstep(0.03, 0.14, q.y) * (1.0 - smoothstep(0.14, 0.34, q.y));
    float tail = (1.0 - smoothstep(0.0, 0.022, abs(q.x))) * tailWindow * fade * step(0.82, rnd);
    return saturate(drop + tail * M.m_wetStreakStrength);
}

float rainDroplets(vec2 uv, float scale, float speed, float density) {
    float a = rainLayer(uv, scale, speed, density, 0.0);
    float b = rainLayer(uv + vec2(0.17, 0.31), scale * 1.65, speed * 1.27, density * 0.72, 19.37) * 0.58;
    return saturate(max(a, b));
}

// ------------------------------------------------------------------ face shading helpers
// Mirror of engine/gfx/FaceShading.h (which the CPU tests exercise): keep constants and formulas in sync.
// 1. nose/under-eye lightening is *per texel* and bounded: inside the ellipse only, it releases a fraction of the SDF
//    cast shadow instead of gating the whole face.
float faceNoseMask(vec2 uv) {
    vec2 d = (uv - vec2(0.5, 0.45)) / vec2(0.12, 0.08);
    return 1.0 - smoothstep(0.60, 1.00, length(d));
}
float faceLightenNoseShadow(float sdfShadow, float mask, float amount) {
    return max(0.0, saturate(sdfShadow) * (1.0 - saturate(amount) * saturate(mask)));
}
// 2. ZZZ normal flatten: the shading normal is lerped toward the view vector, so the light map (not the geometric
//    gradient) shapes the face. A real normal change, applied before any lighting term.
vec3 faceFlattenNormal(vec3 n, vec3 viewDir, float amount) {
    return normalize(mix(n, normalize(viewDir), saturate(amount)));
}
// 3. complementary aura: zero when the face looks at the camera, rising only toward the silhouette.
float faceSilhouetteAura(float dotNV, float strength, float power) {
    return saturate(strength) * pow(1.0 - saturate(dotNV), max(power, 0.001));
}

float computeFaceSDFShadow(vec2 uv, vec3 lightDirWS) {
    bool headDriven = PC.pad0 == 1u;     // VRM face: axes follow the head bone
    vec3 forwardVec = normalize(mat3(PC.model) * (headDriven ? G.faceForward.xyz : M.m_faceForwardOS.xyz));
    vec3 rightVec   = normalize(mat3(PC.model) * (headDriven ? G.faceRight.xyz : M.m_faceRightOS.xyz));
    vec3 upVec = cross(forwardVec, rightVec);
    float sqrUpLen = dot(upVec, upVec);
    vec3 projectedUp = sqrUpLen > 1e-12 ? dot(lightDirWS, upVec) / sqrUpLen * upVec : vec3(0.0);
    vec3 lightHead = lightDirWS - projectedUp;
    float lightHeadLen = length(lightHead);
    if (lightHeadLen <= 1e-5) return 0.0;

    vec3 lightHeadDir = lightHead / lightHeadLen;
    float cosAngle = clamp(dot(lightHeadDir, rightVec), -1.0, 1.0);
    float angle01 = acos(cosAngle) / 3.1415926;
    float exposeRight = step(angle01, 0.5);

    float valueR = pow(saturate(1.0 - angle01 * 2.0), 3.0);
    float valueL = pow(saturate(angle01 * 2.0 - 1.0), 3.0);
    float mixValue = mix(valueL, valueR, exposeRight);

    vec4 sdfTex = texture(tFaceSDF, uv);
    float sdfRight = selectChannel(sdfTex, M.m_sdfChannel);
    float sdfLeft  = selectChannel(texture(tFaceSDF, vec2(1.0 - uv.x, uv.y)), M.m_sdfChannel);
    sdfRight = mix(sdfRight, sdfTex.r, M.m_sdfDirectionalRG);
    sdfLeft  = mix(sdfLeft,  sdfTex.g, M.m_sdfDirectionalRG);
    float swappedRight = sdfLeft;
    float swappedLeft  = sdfRight;
    sdfRight = mix(sdfRight, swappedRight, M.m_sdfSwapRG);
    sdfLeft  = mix(sdfLeft,  swappedLeft,  M.m_sdfSwapRG);
    float mixSdf = mix(sdfRight, sdfLeft, exposeRight);

    float thresholdBias = M.m_sdfThreshold - 0.5;
    float sdfLit = smoothstep(mixValue - M.m_sdfSoftness, mixValue + M.m_sdfSoftness, mixSdf + thresholdBias);
    float frontFade = smoothstep(-M.m_sdfBackFade, M.m_sdfBackFade, dot(lightHeadDir, forwardVec));
    sdfLit = mix(1.0, sdfLit, frontFade);

    // ZZZ nose/under-eye lightening (per texel, inside the ellipse only, bounded by faceNoseLighten). The normal flatten
    // and the silhouette aura are *not* applied here: they belong to the shading terms in main().
    float lightenAmount = M.m_faceNoseLighten * step(0.001, M.m_sdfShadowStrength);
    float shadow = 1.0 - sdfLit;
    return faceLightenNoseShadow(shadow, faceNoseMask(uv), lightenAmount);
}

float splitNprSide() {
    vec2 dir = vec2(G.split.y, G.split.z);
    float isEnabled = step(0.0001, dot(dir, dir));
    vec2 pixelPos = gl_FragCoord.xy;            // Vulkan: origin top-left already
    vec2 perp = vec2(-dir.y, dir.x);
    float splitSide = dot(pixelPos - G.screenParams.xy * 0.5, perp) - G.split.x;
    return isEnabled * step(0.0, splitSide);
}

// ------------------------------------------------------------------ URP replacements
// Main light shadow: 3x3 PCF with hardware comparison (== _SHADOWS_SOFT).
// One cascade (atlas: near = left half, far = right half): 3x3 PCF with hardware comparison; returns -1 when outside.
float shadowCascadeSample(vec4 sc, float cascade, float biasScale, out float border) {
    vec3 p = sc.xyz / sc.w;
    vec2 uv = p.xy * 0.5 + 0.5;
    vec2 e = abs(uv - 0.5) * 2.0;
    border = max(e.x, e.y);
    if (border >= 1.0 || p.z > 1.0) return -1.0;
    float ngl = saturate(dot(normalize(vNormalWS), normalize(G.lightDir.xyz)));
    float lit = 0.0, ref = p.z - G.lightColor.w * biasScale * (1.0 + 4.0 * (1.0 - ngl));   // slope-scaled bias
    vec2 texel = vec2(G.misc.w * 0.5, G.misc.w);
    vec2 base = vec2(uv.x * 0.5 + 0.5 * cascade, uv.y);
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            lit += texture(uShadow, vec3(base + vec2(x, y) * texel, ref));
    return lit / 9.0;
}
// Contact shadow: a soft, cel-edged blob under every character, applied to the world (materials with selfShadow = 1), not to the characters.
float contactAO(vec3 P, vec3 N) {
    float ao = 1.0;
    for (int i = 0; i < 6; ++i) {
        vec4 c = G.contactPos[i];
        if (c.w <= 0.0) continue;
        float r = length(P.xz - c.xz) / c.w, h = abs(P.y - c.y);
        ao *= 1.0 - 0.5 * (1.0 - smoothstep(0.3, 1.0, r)) * saturate(N.y * 2.0) * (1.0 - smoothstep(0.08, 0.45, h));
    }
    return ao;
}
float mainLightShadow(vec4 sc) {
    float b0, b1;
    float bs = mix(6.0, 1.0, M.m_selfShadow);       // characters: large bias = (almost) no self-shadowing, as in Genshin / Guilty Gear; only big occluders (walls, pillars) shade them
    float s0 = shadowCascadeSample(sc, 0.0, bs, b0);
    float s1 = shadowCascadeSample(G.lightViewProj1 * vec4(vPosWS, 1.0), 1.0, bs, b1);
    if (s0 < 0.0) s0 = s1;
    if (s1 < 0.0) return 1.0;                                       // beyond both cascades
    float lit = mix(s0, s1, smoothstep(0.78, 0.96, b0));           // blend into the far cascade near the border of the near one
    return mix(lit, 1.0, smoothstep(0.80, 1.0, b1));               // fade out at the far cascade border
}

// Image based lighting: 9-coefficient SH for diffuse, roughness-selected mip of the equirect map for reflections.
vec3 ambientSH(vec3 n) { return max(shIrradiance(envRotate(n)), vec3(0.0)) * G.envParams.x * G.skyColor.w; }

vec3 environmentReflection(vec3 r, float perceptualRoughness) {
    float lod = perceptualRoughness * (1.7 - 0.7 * perceptualRoughness) * G.envParams.y;
    return textureLod(uEnv, equirectUV(envRotate(r)), lod).rgb * G.envParams.x;
}

// Point-light shadow: pick the cube face from the major axis of the light->surface vector, 3x3 PCF inside that face's tile of the atlas.
float pointShadowFactor(int i, vec3 P) {
    float slot = G.pointShadow[i].x;
    if (slot < 0.0) return 1.0;
    vec3 Lp = G.pointPos[i].xyz, v = P - Lp, a = abs(v);
    float face = a.x >= a.y && a.x >= a.z ? (v.x > 0.0 ? 0.0 : 1.0) : (a.y >= a.z ? (v.y > 0.0 ? 2.0 : 3.0) : (v.z > 0.0 ? 4.0 : 5.0));
    vec3 Pn = P + normalize(vNormalWS) * (G.pointShadow[i].y * (0.5 + 0.5 * length(v)));          // normal offset grows with distance (texel size)
    vec4 sc = G.pointViewProj[int(slot) * 6 + int(face)] * vec4(Pn, 1.0);
    vec3 p = sc.xyz / sc.w;
    if (sc.w <= 0.0 || p.z > 1.0) return 1.0;
    vec2 uv = clamp(p.xy * 0.5 + 0.5, vec2(0.004), vec2(0.996));
    vec2 base = (uv + vec2(face, slot)) * vec2(1.0 / 6.0, 1.0 / 2.0);
    vec2 texel = vec2(1.0 / (512.0 * 6.0), 1.0 / (512.0 * 2.0));
    float lit = 0.0, ref = p.z - G.pointShadow[i].z;
    for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) lit += texture(uPointShadow, vec3(base + vec2(x, y) * texel, ref));
    return mix(lit / 9.0, 1.0, smoothstep(0.85, 1.0, length(v) / G.pointPos[i].w));          // fade at the light's range
}

// Extra point lights: smooth range window * inverse square, shadowed by the cube atlas for the (up to 2) lights that have one.
float pointAtten(int i, vec3 P, out vec3 Lvec) {
    Lvec = G.pointPos[i].xyz - P;
    float d2 = dot(Lvec, Lvec), r = G.pointPos[i].w, w = saturate(1.0 - (d2 * d2) / (r * r * r * r));
    return w * w / (d2 + 1.0) * pointShadowFactor(i, P);
}
vec3 toonPointLights(vec3 N, vec3 P, vec3 albedo) {        // cel-style response used by the NPR side
    vec3 sum = vec3(0.0);
    for (int i = 0; i < 4; ++i) {
        if (G.pointCol[i].r + G.pointCol[i].g + G.pointCol[i].b <= 0.0) continue;
        vec3 Lv; float at = pointAtten(i, P, Lv);
        sum += G.pointCol[i].rgb * at * smoothstep(0.05, 0.5, dot(N, normalize(Lv)));
    }
    return (vec3(1.0) - exp(-sum * 1.1)) * 0.9 * albedo;       // soft knee: a close, bright lamp can add at most ~90% of the albedo (no blown-out toon surfaces)
}

vec3 directBRDF(vec3 N, vec3 V, vec3 L, vec3 diffuse, vec3 specular, float roughness) {
    float roughness2 = roughness * roughness, normalization = roughness * 4.0 + 2.0, r2m1 = roughness2 - 1.0;
    vec3 H = normalize(L + V);
    float NoL = saturate(dot(N, L)), NoH = saturate(dot(N, H)), LoH = saturate(dot(L, H));
    float d = NoH * NoH * r2m1 + 1.00001;
    float spec = roughness2 / ((d * d) * max(0.1, LoH * LoH) * normalization);
    return (diffuse + spec * specular) * NoL;
}

// URP "UniversalFragmentPBR" (simplified BRDF): main light + up to 4 additional point lights + IBL.
vec3 universalFragmentPBR(vec3 N, vec3 V, vec3 P, vec3 L, vec3 lightColorIntensity, float shadowAtten,
                          vec3 albedo, float metallic, float smoothness, float occlusion) {
    float oneMinusReflectivity = (1.0 - metallic) * (1.0 - 0.04);
    float reflectivity = 1.0 - oneMinusReflectivity;
    vec3 diffuse = albedo * oneMinusReflectivity;
    vec3 specular = mix(vec3(0.04), albedo, metallic);
    float perceptualRoughness = 1.0 - smoothness;
    float roughness = max(perceptualRoughness * perceptualRoughness, 0.0078125);
    float roughness2 = roughness * roughness;
    float grazing = saturate(smoothness + reflectivity);

    float NoV = saturate(dot(N, V));
    vec3 gi = ambientSH(N) * diffuse * occlusion;
    vec3 refl = environmentReflection(reflect(-V, N), perceptualRoughness) * occlusion;
    float fres = pow(1.0 - NoV, 4.0);
    float surfaceReduction = 1.0 / (roughness2 + 1.0);
    gi += refl * surfaceReduction * mix(specular, vec3(grazing), fres);
    vec3 color = gi + directBRDF(N, V, L, diffuse, specular, roughness) * lightColorIntensity * shadowAtten;
    for (int i = 0; i < 4; ++i) {
        if (G.pointCol[i].r + G.pointCol[i].g + G.pointCol[i].b <= 0.0) continue;
        vec3 Lv; float at = pointAtten(i, P, Lv);
        color += directBRDF(N, V, normalize(Lv), diffuse, specular, roughness) * G.pointCol[i].rgb * at;
    }
    return color;
}

void main() {
    // robust tangent frame (meshes without tangents would otherwise produce NaNs)
    vec3 Nn = normalize(vNormalWS);
    vec3 Tn = vTangentWS;
    if (dot(Tn, Tn) < 1e-6) Tn = abs(Nn.y) < 0.99 ? cross(vec3(0.0, 1.0, 0.0), Nn) : vec3(1.0, 0.0, 0.0);
    Tn = normalize(Tn - Nn * dot(Nn, Tn));
    vec3 Bn = dot(vBitangentWS, vBitangentWS) < 1e-6 ? cross(Nn, Tn) : normalize(vBitangentWS);
    vec4 baseTex = texture(tBase, vUV) * M.m_baseColor;
    vec4 paramTex = texture(tParam, vUV);
    vec4 styleTex = texture(tStyle, vUV);
    vec4 maskTex = texture(tMask, vUV);
    vec4 highlightMask = texture(tHighlight, vUV);
    vec4 hairSpecStyle = texture(tHairSpec, vUV);
    vec4 emissionTex = texture(tEmission, vUV);
    vec4 flowTex = texture(tFlow, vUV);

    float wetAmount = saturate(M.m_wetness);
    float wetFlowMask = saturate(0.45 + flowTex.r * 0.55 + maskTex.r * 0.12);
    float inferredGloss = smoothstep(0.52, 0.92, 1.0 - paramTex.g);
    float inferredMetal = saturate(paramTex.r * M.m_metallicScale);
    float dropletSurfaceMask = saturate(M.m_wetGlossMask * 0.48 + inferredGloss * 0.46 + inferredMetal * 0.36 + M.m_wetClothMask * 0.08);
    vec3 baseNormalWS = Nn;
    float verticalSurface = smoothstep(0.12, 0.55, 1.0 - abs(baseNormalWS.y));
    vec3 gravityWS = vec3(0.0, -1.0, 0.0);
    vec3 surfaceDownWS = gravityWS - baseNormalWS * dot(gravityWS, baseNormalWS);
    surfaceDownWS = normalize(surfaceDownWS + Bn * 0.0001);
    vec2 surfaceDownUV = normalize(vec2(dot(surfaceDownWS, Tn), dot(surfaceDownWS, Bn)) + vec2(0.0, 0.0001));
    vec2 surfaceSideUV = vec2(-surfaceDownUV.y, surfaceDownUV.x);
    vec2 centeredUv = vUV - 0.5;
    vec2 surfaceRainUv = vec2(dot(centeredUv, surfaceSideUV), dot(centeredUv, -surfaceDownUV)) + 0.5;
    surfaceRainUv += (flowTex.rg - 0.5) * 0.018;
    float rainMask = rainDroplets(surfaceRainUv, M.m_wetDropletScale, M.m_wetDropletSpeed, M.m_wetDropletDensity);
    rainMask = saturate(rainMask * wetAmount * wetFlowMask * dropletSurfaceMask * (0.08 + verticalSurface * 1.22));
    float clothWet = wetAmount * M.m_wetClothMask * wetFlowMask;
    float glossWet = wetAmount * saturate(M.m_wetGlossMask + rainMask * 0.75);
    baseTex.rgb = mix(baseTex.rgb, baseTex.rgb * M.m_wetDarkenColor.rgb, clothWet * M.m_wetDarken);

    float alpha = baseTex.a * M.m_alpha;
    alpha = saturate(min(max(gl_FrontFacing ? 1.0 : 0.0, M.m_doubleSided), alpha));
    if (alpha - M.m_alphaClip < 0.0) discard;

    vec3 normalTS = texture(tNormal, vUV).xyz * 2.0 - 1.0;
    normalTS = normalize(mix(vec3(0, 0, 1), normalTS, M.m_normalStrength));
    normalTS.xy += vec2(dFdx(rainMask), dFdy(rainMask)) * M.m_wetNormalStrength * 1.35;
    normalTS = normalize(normalTS);
    mat3 tbn = mat3(Tn, Bn, Nn);
    vec3 N = normalize(tbn * normalTS);
    vec3 V = normalize(G.cameraPos.xyz - vPosWS);
    // ZZZ face normal flatten (faces only: PC.pad0 == 1 marks the head-bone-driven face SDF draws). Flattening the
    // *shading* normal keeps the geometric normal for the tangent frame / matcap / reflections.
    bool isFaceDraw = PC.pad0 == 1u;
    vec3 Nshade = isFaceDraw ? faceFlattenNormal(N, V, M.m_faceNormalFlatten) : N;

    float shadowAtten = mainLightShadow(vShadowCoord);
    shadowAtten = mix(shadowAtten, smoothstep(0.35, 0.65, shadowAtten), M.m_celShadow);      // anime: crisp, cel-shaped cast shadow edge
    vec3 L = normalize(G.lightDir.xyz);
    vec3 lightRGB = G.lightColor.rgb * G.lightDir.w;
    vec3 H = normalize(L + V);
    float NoL = saturate(dot(Nshade, L));
    float NoV = saturate(dot(Nshade, V));
    float NoH = saturate(dot(Nshade, H));
    float halfLambert = NoL * 0.5 + 0.5;

    float metallic = saturate(paramTex.r * M.m_metallicScale);
    float baseSmoothness = saturate(mix(M.m_smoothnessMin, M.m_smoothnessMax, 1.0 - paramTex.g) * M.m_smoothnessScale);
    float smoothness = saturate(baseSmoothness + glossWet * M.m_wetSmoothnessBoost);
    float occlusion = mix(1.0, saturate(paramTex.b), M.m_occlusionStrength);
    vec3 pbrColor = universalFragmentPBR(Nshade, V, vPosWS, L, lightRGB, shadowAtten, baseTex.rgb, metallic, smoothness, occlusion);

    float rampU = saturate(smoothstep(M.m_rampThreshold - M.m_rampFeather, M.m_rampThreshold + M.m_rampFeather, halfLambert));
    rampU = mix(rampU, styleTex.r, M.m_styleRampStrength);
    vec3 rampColor = texture(tRamp, vec2(rampU, 0.5)).rgb;
    vec3 lutBase = mix(sampleLut(baseTex.rgb), baseTex.rgb, M.m_faceColorStabilize);
    // The Unity NPR base colour is unlit by design; make it follow the sun / moon colour and intensity so the time of day matters (1.0 for the default daylight)
    vec3 toonTint = clamp(lightRGB / (vec3(1.0, 0.92, 0.82) * 0.95), vec3(0.0), vec3(1.0));
    toonTint = min(max(toonTint, G.skyColor.rgb * 0.38), vec3(1.0));
    lutBase *= toonTint;
    float wrappedLight = mix(NoL, halfLambert, M.m_nprLightWrap);
    float nprNormalShade = mix(1.0, mix(0.92, 1.08, saturate(wrappedLight)), M.m_nprNormalShadeStrength);
    vec3 nprLitBase = lutBase * nprNormalShade * M.m_nprLitBoost;
    // anime shading: the shade colour picks up the sky (cool ambient fill) instead of just darkening the albedo
    vec3 skyShade = mix(vec3(1.0), G.skyColor.rgb / max(max(G.skyColor.r, G.skyColor.g), max(G.skyColor.b, 0.05)), 0.30);
    vec3 rampTone = lutBase * rampColor * M.m_shadowColor.rgb * skyShade;
    rampTone *= mix(1.0, 0.82, (1.0 - smoothstep(0.04, 0.22, halfLambert)) * M.m_celShadow);      // second, deeper shade band (Genshin 'outer shadow')

    float sdfRawShadow = computeFaceSDFShadow(vUV, L);
    float sdfShadow = sdfRawShadow;
    vec3 sdfTone = lutBase * M.m_sdfShadowColor.rgb;
    rampTone = mix(rampTone, sdfTone, sdfShadow * M.m_sdfShadowStrength);

    float realtimeShadow = (1.0 - saturate(shadowAtten)) * M.m_realtimeShadowStrength;
    float rampShadow = 1.0 - rampU;

    if (M.m_debugMode > 0.5) {
        vec4 sdfTex = texture(tFaceSDF, vUV);
        float sdfChannel = selectChannel(sdfTex, M.m_sdfChannel);
        float dm = M.m_debugMode;
        vec3 dbg = baseTex.rgb;
        dbg = mix(dbg, paramTex.rgb,  step(1.5, dm) * (1.0 - step(2.5, dm)));
        dbg = mix(dbg, styleTex.rgb,  step(2.5, dm) * (1.0 - step(3.5, dm)));
        dbg = mix(dbg, maskTex.rgb,   step(3.5, dm) * (1.0 - step(4.5, dm)));
        dbg = mix(dbg, sdfTex.rgb,    step(4.5, dm) * (1.0 - step(5.5, dm)));
        dbg = mix(dbg, vec3(sdfChannel),   step(5.5, dm) * (1.0 - step(6.5, dm)));
        dbg = mix(dbg, vec3(sdfShadow),    step(6.5, dm) * (1.0 - step(7.5, dm)));
        dbg = mix(dbg, vec3(realtimeShadow), step(7.5, dm) * (1.0 - step(8.5, dm)));
        dbg = mix(dbg, vec3(rampShadow),   step(8.5, dm) * (1.0 - step(9.5, dm)));
        // Sakuga extensions
        dbg = mix(dbg, N * 0.5 + 0.5,      step(9.5, dm) * (1.0 - step(10.5, dm)));
        dbg = mix(dbg, vec3(NoL),          step(10.5, dm) * (1.0 - step(11.5, dm)));
        dbg = mix(dbg, vec3(shadowAtten),  step(11.5, dm) * (1.0 - step(12.5, dm)));
        dbg = mix(dbg, vec3(fract(vUV), 0.0), step(12.5, dm));
        outColor = vec4(encodeOutput(saturate(dbg * M.m_debugExposure)), alpha);
        return;
    }

    float stableRampShadow = mix(rampShadow, 0.0, M.m_faceColorStabilize);
    float stylizedShadowMask = saturate(max(max(realtimeShadow, stableRampShadow), sdfShadow * M.m_sdfShadowStrength));
    float pbrBaseBlend = mix(M.m_pbrBaseBlend, 0.0, M.m_faceColorStabilize);
    vec3 litBaseColor = mix(nprLitBase, pbrColor, pbrBaseBlend);
    vec3 hybridColor = mix(litBaseColor, rampTone, stylizedShadowMask * M.m_rampBlend);
    hybridColor = max(hybridColor, lutBase * M.m_shadowFloor * stylizedShadowMask);

    float nprShadowMask = saturate(max(stableRampShadow, sdfShadow * M.m_sdfShadowStrength));
    vec3 pureNprColor = mix(nprLitBase, rampTone, nprShadowMask * M.m_rampBlend);
    pureNprColor = max(pureNprColor, lutBase * M.m_shadowFloor * nprShadowMask);

    vec2 specUV = vec2(NoH, saturate(paramTex.a));
    vec3 specRamp = texture(tSpecRamp, specUV).rgb;
    hybridColor += specRamp * maskTex.r * M.m_specRampStrength * pow(NoH, 32.0) * saturate(NoL + 0.2);
    hybridColor += specRamp * (rainMask + glossWet * 0.35) * M.m_wetSpecBoost * pow(NoH, 48.0) * saturate(NoL + 0.25);
    hybridColor += rainMask * M.m_wetDropletVisibility * mix(0.25, 0.85, NoV) * saturate(NoL + 0.35);
    float rainCore = smoothstep(0.28, 0.88, rainMask);
    float rainEdge = saturate(length(vec2(dFdx(rainMask), dFdy(rainMask))) * 10.0);
    float rainReflect = pow(saturate(dot(reflect(-L, N), V)), 64.0);
    float rainGlint = rainCore * (0.35 + rainEdge) * (pow(NoH, 96.0) * 1.4 + rainReflect * 1.15) * saturate(NoL + 0.35);
    hybridColor += rainGlint * M.m_wetDropletVisibility * M.m_wetSpecBoost * vec3(1.0, 0.96, 0.88);

    vec3 T = Tn;
    float hairSinTH = sqrt(saturate(1.0 - dot(T, H) * dot(T, H)));
    float hairSpec = pow(saturate(hairSinTH + M.m_hairSpecShift), M.m_hairAnisoPower);
    float hairMask = saturate(hairSpecStyle.r + hairSpecStyle.g * 0.5 + maskTex.r * 0.25);
    hybridColor += hairSpec * hairMask * specRamp * baseTex.rgb * M.m_hairAnisoStrength * saturate(NoL + 0.35);

    vec3 normalVS = normalize(mat3(G.view) * N);
    vec2 matcapUV = vec2(normalVS.x, -normalVS.y) * 0.5 + 0.5;   // -y: our proj flips Y, Unity matcaps are authored y-up
    vec3 matcap = textureLod(tMatCap, matcapUV, 0.0).rgb;   // explicit LOD: flat-shaded meshes give zero UV derivatives (NaN with anisotropic filtering)
    hybridColor += matcap * M.m_matCapStrength * (1.0 - M.m_faceColorStabilize) * maskTex.g;

    float faceHighlight = saturate(highlightMask.r + highlightMask.g * 0.5);
    vec3 stylizedAdd = faceHighlight * M.m_highlightStrength * mix(0.4, 1.0, NoL) * baseTex.rgb;

    float emissionMask = saturate(emissionTex.a + emissionTex.r + flowTex.r * 0.35);
    stylizedAdd += emissionTex.rgb * M.m_emissionColor.rgb * emissionMask * M.m_emissionStrength;

    float rim = pow(1.0 - NoV, M.m_rimPower) * M.m_rimIntensity;
    stylizedAdd += rim * M.m_rimColor.rgb;
    // anime rim light: a thin bright edge on the side facing the sun / moon (not inside cast shadows), tinted by the light colour
    {
        float sunRim = pow(1.0 - NoV, 3.0) * smoothstep(-0.15, 0.55, dot(N, L)) * smoothstep(0.15, 0.8, shadowAtten);
        stylizedAdd += sunRim * M.m_rimIntensity * 1.8 * mix(vec3(1.0), G.lightColor.rgb, 0.6) * mix(0.35, 1.0, saturate(G.lightDir.w));
    }
    // ZZZ complementary aura on faces: additive, silhouette only (0 where the face looks at the camera), tinted by the
    // complement of the key light. Face materials ship with rimIntensity = 0, so nothing else adds a rim there.
    if (isFaceDraw) {
        float aura = faceSilhouetteAura(dot(Nshade, V), M.m_faceAuraStrength, M.m_faceAuraPower);
        vec3 auraTint = mix(M.m_rimColor.rgb, vec3(1.0) - clamp(lightRGB, 0.0, 1.0), 0.5);
        stylizedAdd += aura * auraTint * mix(0.35, 1.0, saturate(G.lightDir.w));
    }

    vec3 toonPts = toonPointLights(N, vPosWS, lutBase);
    hybridColor += stylizedAdd + toonPts * (1.0 - pbrBaseBlend);
    pureNprColor += stylizedAdd + toonPts;
    // pure-NPR extras (the hybrid path gets these from PBR / IBL): hair angel ring, stepped specular, matcap, sky-tinted shade
    {
        float hairOn = step(0.001, M.m_hairAnisoStrength);
        float ring = (1.0 - smoothstep(0.05, 0.15, abs(normalVS.y - 0.42))) * saturate(NoL + 0.3) * max(hairMask, 0.7);
        float ringW = 1.0 - smoothstep(0.06, 0.14, abs(normalVS.y - 0.42 + 0.10 * normalVS.x * normalVS.x));        // slightly curved, camera-space band (angel ring, matcap-like)
        float ringHybrid = ringW * saturate(NoL * 0.6 + 0.55) * max(hairMask, 0.75) * hairOn;
        pureNprColor += hairOn * ring * min(M.m_hairAnisoStrength, 1.6) * 0.30 * mix(baseTex.rgb, vec3(1.0), 0.6);
        hybridColor += ringHybrid * min(M.m_hairAnisoStrength, 1.6) * 1.0 * mix(baseTex.rgb, vec3(1.0), 0.55) * saturate(lightRGB * 0.9 + 0.25);
        pureNprColor += step(0.97, NoH) * maskTex.r * M.m_specRampStrength * specRamp * saturate(NoL + 0.1) * 0.7;
        pureNprColor += matcap * M.m_matCapStrength * 0.6 * (1.0 - M.m_faceColorStabilize) * maskTex.g;
        pureNprColor = mix(pureNprColor, pureNprColor * (0.7 + 0.6 * G.skyColor.rgb), nprShadowMask * 0.35);
    }

    // artificial subsurface scattering (Genshin): a warm band that hugs the terminator and the cast-shadow penumbra; goes cool-violet at night; none on faces
    {
        float termRamp = 1.0 - abs(rampU * 2.0 - 1.0), termShadow = (1.0 - abs(saturate(shadowAtten) * 2.0 - 1.0)) * M.m_realtimeShadowStrength;
        float term = max(termRamp * termRamp, termShadow * termShadow) * step(0.001, NoL + 0.5);
        vec3 sssTint = mix(vec3(1.0, 0.50, 0.38), vec3(0.62, 0.50, 1.0), saturate(G.skyParams.x));
        vec3 sss = lutBase * sssTint * term * M.m_sssStrength * (1.0 - M.m_faceColorStabilize) * saturate(G.lightDir.w + 0.3);
        hybridColor += sss; pureNprColor += sss;
    }
    vec3 color = mix(hybridColor, pureNprColor, max(splitNprSide(), G.style.x));
    color *= mix(1.0, contactAO(vPosWS, normalize(vNormalWS)), M.m_selfShadow);
    color *= mix(1.0, 0.62, saturate(G.skyParams.x));                       // night: keep white clothes from glowing in the dark
    float dist = length(G.cameraPos.xyz - vPosWS);
    float fog = 1.0 - exp2(-G.fogColor.w * G.fogColor.w * dist * dist * 1.442695);
    color = mix(color, G.fogColor.rgb, clamp(fog, 0.0, 1.0));
    outColor = vec4(encodeOutput(color), alpha);
}
