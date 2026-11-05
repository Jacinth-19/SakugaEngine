// Shared display finishing logic. Optional geometryInk is declared by depth variants.
float luma(vec3 c) { return dot(c, vec3(.2126,.7152,.0722)); }
vec3 sampleAt(vec2 uv) { return textureLod(tDisplay, clamp(uv, vec2(0), vec2(1)), 0.0).rgb; }
void main() {
    vec2 t = 1.0 / vec2(textureSize(tDisplay, 0));
    vec3 c = sampleAt(vUV);
    if (pc.a.x > 0.0) {
        vec2 shift = (vUV - 0.5) * t * pc.a.x;
        c.r = sampleAt(vUV + shift).r; c.b = sampleAt(vUV - shift).b;
    }
    if (pc.a.y > 0.0 || pc.a.w > 0.0) {
        vec3 n = sampleAt(vUV + vec2(0,-t.y)), s = sampleAt(vUV + vec2(0,t.y));
        vec3 e = sampleAt(vUV + vec2(t.x,0)), w = sampleAt(vUV + vec2(-t.x,0));
        float edge = length(vec2(luma(e)-luma(w), luma(n)-luma(s)));
        if (pc.a.w > 0.0) {
            vec2 gradient = vec2(luma(e)-luma(w), luma(s)-luma(n));
            vec2 tangent = length(gradient) > .001 ? normalize(vec2(-gradient.y, gradient.x)) : vec2(1.0, 0.0);
            vec2 stroke = tangent * t * 2.5;
            vec3 painted = (c*2.0 + sampleAt(vUV+stroke) + sampleAt(vUV-stroke)) * .25;
            // Screen-anchored directional bristle variation, strongest on flat regions.
            float bristle = sin(dot(gl_FragCoord.xy, vec2(-tangent.y,tangent.x))*2.1) * 0.012;
            float blend = clamp(pc.a.w * (1.0-smoothstep(.03,.25,edge)), 0.0, 1.0);
            c = mix(c, painted * (1.0 + bristle), blend);
        }
        c *= 1.0 - clamp(edge * pc.a.y * 3.0, 0.0, .85);
    }
#ifdef SAKUGA_DEPTH_INK
    c *= 1.0 - clamp(geometryInk(vUV) , 0.0, .85);
#endif
    if (pc.a.z >= 2.0) {
        float levels = pc.a.z - 1.0;
        float dither = (mod(gl_FragCoord.x + gl_FragCoord.y * 2.0, 4.0) / 4.0 - .375) / levels;
        c = floor(clamp(c + dither, 0.0, 1.0) * levels + .5) / levels;
    }
    outColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
