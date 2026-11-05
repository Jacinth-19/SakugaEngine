// Bounded single-scattering aerial perspective for opaque geometry, using the
// same transmittance/model and approximate multi-order LUT as the sky.
const vec3 aerialBetaR = vec3(.005802,.013558,.033100);
const vec3 aerialBetaO = vec3(.000650,.001881,.000085);
const float aerialBetaM = .003996;
vec3 aerialRotate(vec4 q, vec3 v) { return v + 2.0 * cross(q.xyz, cross(q.xyz,v) + q.w*v); }
vec3 aerialSunT(float mu, float altitude) {
    float r = pc.f.x + clamp(altitude, 0.0, pc.f.y);
    float horizon = -sqrt(max(0.0, 1.0 - (pc.f.x/r)*(pc.f.x/r)));
    if (mu < horizon) return vec3(0.0);
    vec2 ij = vec2(sqrt(clamp((mu-horizon)/(1.0-horizon),0.0,1.0))*255.0,
                   clamp(altitude/pc.f.y,0.0,1.0)*63.0);
    vec3 encoded = textureLod(tTransmittance, (ij+0.5)/vec2(256.0,64.0), 0.0).rgb;
    return encoded * encoded;
}
vec3 aerialMultiple(float mu, float altitude) {
    float u = 0.5 + 0.5 * sign(mu) * sqrt(abs(clamp(mu, -1.0, 1.0)));
    vec2 ij = vec2(u * 32.0, clamp(altitude / pc.f.y, 0.0, 1.0) * 15.0);
    vec3 e = textureLod(tMultipleScattering, (ij + 0.5) / vec2(33.0, 16.0), 0.0).rgb;
    return e * e * 16.0;
}
vec3 aerialForDepth(vec3 scene, float depth, vec2 uv) {
    if (depth >= 1.0) return scene;
    float z = abs(pc.a.w) / max(abs(depth + pc.a.z), 1e-7);
    vec3 viewRay = vec3((uv*2.0-1.0)/pc.a.xy, -1.0);
    float distanceKm = min(length(viewRay) * z * .001, 150.0);
    vec3 d = normalize(aerialRotate(pc.d, normalize(viewRay)));
    vec3 sun = normalize(pc.e.xyz);
    float ct = clamp(dot(d,sun), -1.0, 1.0), g = .76;
    float rayPhase = .75*(1.0+ct*ct);
    float miePhase = min(12.0, (1.0-g*g)/pow(max(1.0+g*g-2.0*g*ct,.02),1.5));
    float r = pc.f.x + clamp(pc.c.z,0.0,pc.f.y);
    float top = pc.f.x + pc.f.y, rm = r*d.y;
    float topDistance = max(0.0, -rm + sqrt(max(0.0, rm*rm + (top-r)*(top+r))));
    distanceKm = min(distanceKm, topDistance);
    float groundDisc = rm*rm + (pc.f.x-r)*(pc.f.x+r);
    if (d.y < 0.0 && groundDisc > 1e-7)
        distanceKm = min(distanceKm, max(0.0, -rm - sqrt(groundDisc)));
    float ds = distanceKm / 8.0;
    vec3 trans = vec3(1), radiance = vec3(0);
    for (int i=0; i<8; ++i) {
        float t = (float(i)+.5)*ds;
        vec3 pos = vec3(d.x*t, r+d.y*t, d.z*t);
        float radius = length(pos), h = max(0.0, radius-pc.f.x);
        float dr = exp(-h/pc.f.z), dm = exp(-h/pc.f.w);
        float ozone = max(0.0, 1.0-abs(h-25.0)/15.0);
        vec3 extinction = aerialBetaR*dr + vec3(aerialBetaM*dm) + aerialBetaO*ozone*pc.g.x;
        vec3 attenuation = exp(-extinction*ds);
        vec3 sunT = aerialSunT(dot(pos/radius,sun), h);
        vec3 source = aerialBetaR*dr*rayPhase*pc.g.y + vec3(aerialBetaM*dm*miePhase*pc.g.z);
        vec3 multiple = aerialMultiple(dot(pos/radius,sun), h);
        vec3 multiSource = multiple * (aerialBetaR*dr*pc.g.y + vec3(aerialBetaM*dm*pc.g.z));
        radiance += trans*sqrt(attenuation)*(sunT*source + multiSource)*ds;
        trans *= attenuation;
    }
    return scene*trans + radiance*pc.h.rgb*pc.h.w;
}
