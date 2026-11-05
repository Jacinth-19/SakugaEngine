// Shared one-pixel source pass: only clear-depth sun-disc samples contribute.
float sunClearCoverage(vec2 uv);
void main() {
    vec2 pos=pc.f.xy, radius=vec2(pc.f.z/max(pc.d.w,.01),pc.f.z);
    vec2 offsets[5]=vec2[5](vec2(0),vec2(.5,0),vec2(-.5,0),vec2(0,.5),vec2(0,-.5));
    vec3 source=vec3(0.0);
    for(int i=0;i<5;++i) {
        vec2 at=pos+offsets[i]*radius;
        if (all(greaterThanEqual(at,vec2(0))) && all(lessThanEqual(at,vec2(1))))
            source+=min(max(textureLod(tScene,at,0.0).rgb-vec3(1.0),vec3(0.0)),vec3(16.0))*sunClearCoverage(at);
    }
    outColor=vec4(source/5.0,1.0);
}
