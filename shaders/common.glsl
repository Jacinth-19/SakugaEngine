// Shared declarations for all Sakuga shaders (set 0 = per-frame, set 1 = material, push constants = per-draw).
#ifndef SK_COMMON_GLSL
#define SK_COMMON_GLSL

layout(set = 0, binding = 0, std140) uniform GlobalUBO {
    mat4 viewProj;
    mat4 view;
    mat4 proj;
    mat4 lightViewProj;
    vec4 cameraPos;      // xyz, w = time (seconds)
    vec4 screenParams;   // width, height, 1/width, 1/height
    vec4 lightDir;       // xyz = direction TOWARDS the light, w = intensity
    vec4 lightColor;     // rgb, w = shadow depth bias
    vec4 skyColor;       // rgb, w = ambient intensity
    vec4 groundColor;    // rgb
    vec4 fogColor;       // rgb, w = density (exp2)
    vec4 misc;           // x exposure, y tonemap (0/1), z wet preview time (0 = use real time), w shadow texel size
    vec4 split;          // x split offset px, y dir.x, z dir.y, w global outline scale
    vec4 sh[9];          // diffuse SH (irradiance / pi), rgb
    vec4 envParams;      // x intensity, y max LOD, z 1 = HDR environment drives the sky, w rotation (rad)
    vec4 pointPos[4];    // xyz position, w range
    vec4 pointCol[4];    // rgb colour * intensity (intensity 0 = off)
    vec4 faceForward;    // head frame (model space) for draws with PC.pad0 == 1
    vec4 faceRight;
    vec4 skyZenith;      // rgb zenith colour, w = cloud cover
    vec4 skyHorizon;     // rgb horizon colour, w = cloud time
    vec4 skyParams;      // x star visibility, y sun disc intensity, z sun size
    vec4 style;          // x = 1: every material uses the pure NPR path
    mat4 lightViewProj1; // far shadow cascade (atlas: near = left half, far = right half)
    vec4 shadowCascade;  // x near radius, y far radius, w cascade count
    vec4 contactPos[6];  // xyz ground contact point of a character, w radius (0 = unused)
    mat4 pointViewProj[24];   // cube-face matrices for four shadow-casting point lights
    vec4 pointShadow[4];      // x = shadow slot of point light i (-1 none), y = normal offset, z = depth bias
} G;

layout(set = 0, binding = 1, std430) readonly buffer BoneBuffer { mat4 bones[]; };
layout(set = 0, binding = 4, std430) readonly buffer MorphDeltas { vec4 morphDelta[]; };
layout(set = 0, binding = 5, std430) readonly buffer MorphWeights { float morphWeight[]; };
layout(set = 0, binding = 2) uniform sampler2DShadow uShadow;
layout(set = 0, binding = 6) uniform sampler2DShadow uPointShadow;      // 6 cube faces per light, 4 lights (rows)
layout(set = 0, binding = 3) uniform sampler2D uEnv;     // equirectangular environment, RGBA16F with mips

vec3 envRotate(vec3 d) { float c = cos(G.envParams.w), s = sin(G.envParams.w); return vec3(c * d.x + s * d.z, d.y, -s * d.x + c * d.z); }
vec2 equirectUV(vec3 d) { d = normalize(d); return vec2(atan(d.z, d.x) * 0.15915494 + 0.5, acos(clamp(d.y, -1.0, 1.0)) * 0.31830989); }
vec3 shIrradiance(vec3 n) {
    return G.sh[0].rgb * 0.282095 + G.sh[1].rgb * (0.488603 * n.y) + G.sh[2].rgb * (0.488603 * n.z) + G.sh[3].rgb * (0.488603 * n.x)
         + G.sh[4].rgb * (1.092548 * n.x * n.y) + G.sh[5].rgb * (1.092548 * n.y * n.z) + G.sh[6].rgb * (0.315392 * (3.0 * n.z * n.z - 1.0))
         + G.sh[7].rgb * (1.092548 * n.x * n.z) + G.sh[8].rgb * (0.546274 * (n.x * n.x - n.y * n.y));
}

layout(push_constant) uniform PushConstants {
    mat4 model;
    uint boneOffset;
    uint skinned;
    uint pad0;
    uint pad1;
    uint morphWeightOffset;
    uint morphCount;
    uint morphBase;
    uint vertexCount;
} PC;

// The scene is rendered in linear HDR (RGBA16F); exposure, tonemapping, grading and the sRGB OETF happen in the post pass.
vec3 encodeOutput(vec3 c) { return max(c, vec3(0.0)); }

#endif
