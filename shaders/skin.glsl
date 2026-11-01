// Vertex-stage helpers: GPU skinning (linear blend). Included after common.glsl.
#ifndef SK_SKIN_GLSL
#define SK_SKIN_GLSL
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inUV;
layout(location = 4) in uvec4 inJoints;
layout(location = 5) in vec4 inWeights;

// Morph targets: position deltas of every slot, weighted by the per-draw weights (VRM expressions such as blink).
vec3 morphedPos() {
    vec3 p = inPos;
    for (uint i = 0u; i < PC.morphCount; ++i) {
        float w = morphWeight[PC.morphWeightOffset + i];
        if (w > 0.0005) p += morphDelta[PC.morphBase + i * PC.vertexCount + uint(gl_VertexIndex)].xyz * w;
    }
    return p;
}

mat4 skinMatrix() {
    if (PC.skinned == 0u) return mat4(1.0);
    float wsum = inWeights.x + inWeights.y + inWeights.z + inWeights.w;
    mat4 m = inWeights.x * bones[PC.boneOffset + inJoints.x]
           + inWeights.y * bones[PC.boneOffset + inJoints.y]
           + inWeights.z * bones[PC.boneOffset + inJoints.z]
           + inWeights.w * bones[PC.boneOffset + inJoints.w];
    return wsum > 0.0001 ? m / wsum : mat4(1.0);
}
#endif
