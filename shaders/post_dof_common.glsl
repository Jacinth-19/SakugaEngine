// Artistic depth of field: a small 9-tap circle sampled from a linear HDR colour input.
vec3 blurDof(sampler2D src, vec2 uv, float radiusPx, vec2 texel) {
    vec2 t = texel * radiusPx;
    vec3 s = textureLod(src, uv, 0.0).rgb * 4.0;
    s += (textureLod(src, uv + vec2(t.x,0), 0.0).rgb + textureLod(src, uv - vec2(t.x,0), 0.0).rgb
        + textureLod(src, uv + vec2(0,t.y), 0.0).rgb + textureLod(src, uv - vec2(0,t.y), 0.0).rgb) * 2.0;
    s += textureLod(src, uv+t, 0.0).rgb + textureLod(src, uv-t, 0.0).rgb
       + textureLod(src, uv+vec2(t.x,-t.y), 0.0).rgb + textureLod(src, uv+vec2(-t.x,t.y), 0.0).rgb;
    return s / 16.0;
}
float dofRadius(float depth, vec4 a, float maxRadius) {
    float z = depth >= 1.0 ? 100000.0 : abs(a.y) / max(abs(depth + a.x), 1e-7);
    return maxRadius * clamp(abs(z - a.z) / max(a.w, .01), 0.0, 1.0);
}
