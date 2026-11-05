// Depth-aware 9-tap HDR gather. readDofDepth is provided by each depth variant.
// This reduces cross-layer colour bleeding; it is not a scatter/occlusion-complete DoF.
float readDofDepth(vec2 uv);
float dofDistance(float depth, vec4 a) {
    return depth >= 1.0 ? 100000.0 : abs(a.y) / max(abs(depth + a.x), 1e-7);
}
float dofRadius(float depth, vec4 a, float maxRadius) {
    return maxRadius * clamp(abs(dofDistance(depth, a) - a.z) / max(a.w, .01), 0.0, 1.0);
}
float dofDepthWeight(float centreDepth, float tapDepth, vec4 a) {
    bool centreSky = centreDepth >= 1.0, tapSky = tapDepth >= 1.0;
    if (centreSky != tapSky) return 0.0;
    if (centreSky) return 1.0;
    float z0 = dofDistance(centreDepth, a), z1 = dofDistance(tapDepth, a);
    float tolerance = max(.05, min(z0,z1)*.08 + max(a.w,.01)*.05);
    return exp2(-abs(z0-z1)/tolerance);
}
vec3 blurDof(sampler2D src, vec2 uv, float centreDepth, vec4 projFocus, float radiusPx, vec2 texel) {
    if (radiusPx <= 0.001) return textureLod(src, uv, 0.0).rgb;
    vec2 t = texel * radiusPx;
    const vec2 offsets[8] = vec2[8](vec2(1,0), vec2(-1,0), vec2(0,1), vec2(0,-1),
                                    vec2(1,1), vec2(-1,-1), vec2(1,-1), vec2(-1,1));
    vec3 sum = textureLod(src, uv, 0.0).rgb * 4.0; float total = 4.0;
    for (int i=0; i<8; ++i) {
        vec2 coord = clamp(uv + offsets[i]*t, vec2(0.0), vec2(1.0));
        float w = (i < 4 ? 2.0 : 1.0) * dofDepthWeight(centreDepth, readDofDepth(coord), projFocus);
        sum += textureLod(src, coord, 0.0).rgb * w; total += w;
    }
    return sum / total;
}
