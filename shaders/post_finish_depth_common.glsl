// Scene-depth silhouettes and normals reconstructed from depth. Transparent surfaces
// without depth writes and normal-map detail cannot contribute.
float finishDepth(vec2 uv);
vec3 finishPosition(vec2 uv, float d) {
    float z = abs(pc.b.w) / max(abs(d + pc.b.z), 1e-7);
    return vec3((uv * 2.0 - 1.0) * z / pc.b.xy, -z);
}
float depthDifference(float a, float b) {
    if ((a >= 1.0) != (b >= 1.0)) return 1.0;
    if (a >= 1.0) return 0.0;
    float za = abs(pc.b.w) / max(abs(a + pc.b.z), 1e-7);
    float zb = abs(pc.b.w) / max(abs(b + pc.b.z), 1e-7);
    return smoothstep(.01, .10, abs(za-zb) / max(min(za,zb), .01));
}
vec3 depthNormal(vec2 uv, vec2 texel) {
    float d = finishDepth(uv);
    if (d >= 1.0) return vec3(0.0);
    vec2 l=clamp(uv-vec2(texel.x,0),vec2(0),vec2(1)), r=clamp(uv+vec2(texel.x,0),vec2(0),vec2(1));
    vec2 u=clamp(uv-vec2(0,texel.y),vec2(0),vec2(1)), v=clamp(uv+vec2(0,texel.y),vec2(0),vec2(1));
    float dl=finishDepth(l), dr=finishDepth(r), du=finishDepth(u), dv=finishDepth(v);
    if (dl>=1.0 && dr>=1.0 || du>=1.0 && dv>=1.0) return vec3(0.0);
    bool right = dr<1.0 && (dl>=1.0 || abs(dr-d)<abs(dl-d));
    bool down = dv<1.0 && (du>=1.0 || abs(dv-d)<abs(du-d));
    vec3 p=finishPosition(uv,d);
    vec3 dx = right ? finishPosition(r,dr)-p : p-finishPosition(l,dl);
    vec3 dy = down ? finishPosition(v,dv)-p : p-finishPosition(u,du);
    vec3 n=cross(dx,dy); float n2=dot(n,n);
    return n2 > 1e-16 ? n*inversesqrt(n2) : vec3(0.0);
}
float geometryInk(vec2 uv) {
    vec2 t = 1.0 / vec2(textureSize(tDisplay,0));
    float d=finishDepth(uv);
    vec2 r=clamp(uv+vec2(t.x,0),vec2(0),vec2(1)), v=clamp(uv+vec2(0,t.y),vec2(0),vec2(1));
    float edge=0.0;
    if (pc.c.x > 0.0) edge = max(depthDifference(d,finishDepth(r)),depthDifference(d,finishDepth(v))) * pc.c.x;
    if (pc.c.y > 0.0 && d < 1.0) {
        vec3 n=depthNormal(uv,t), nr=depthNormal(r,t), nv=depthNormal(v,t);
        float er = dot(n,n)>.5 && dot(nr,nr)>.5 ? 1.0-clamp(dot(n,nr),0.0,1.0) : 0.0;
        float ev = dot(n,n)>.5 && dot(nv,nv)>.5 ? 1.0-clamp(dot(n,nv),0.0,1.0) : 0.0;
        edge = max(edge, smoothstep(.03,.25,max(er,ev)) * pc.c.y);
    }
    return edge;
}
