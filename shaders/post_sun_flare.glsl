// A deliberately simple sun-specific screen-space flare, not a lens simulation.
// The source is masked by clear-depth coverage. Transparent/cloud occlusion is approximate.
vec3 sunFlare(vec2 uv) {
    if (pc.f.w <= 0.0) return vec3(0.0);
    vec2 pos=pc.f.xy;
    vec3 source=textureLod(tSunVisibility, vec2(.5), 0.0).rgb;
    // A few soft, sun-axis ghosts. Their shape/energy is artistic and intentionally bounded.
    vec2 axis=pos-.5;
    vec2 p1=vec2(.5)-axis*.65, p2=vec2(.5)+axis*.30;
    vec2 aspect=vec2(pc.d.w,1.0);
    float a=exp(-dot((uv-p1)*aspect,(uv-p1)*aspect)/.003);
    float b=exp(-dot((uv-p2)*aspect,(uv-p2)*aspect)/.008);
    return source * pc.f.w * (a*vec3(.55,.72,1.0)+b*vec3(1.0,.65,.40)*.5);
}
