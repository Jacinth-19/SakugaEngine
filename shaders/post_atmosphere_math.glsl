// Shared by single-sample and MSAA versions. p = (projection[0][0], [1][1], [2][2], [3][2]).
float fogForDepth(float depth, vec2 uv, vec4 p, float density) {
    // Clear depth is sky. Do not fog it again; the sky shader already contains atmosphere and clouds.
    if (depth >= 1.0) return 0.0;
    float z = abs(p.w) / max(abs(depth + p.z), 1e-7);
    vec2 xy = (uv * 2.0 - 1.0) * z / p.xy;
    float d = length(vec3(xy, z)) * density;
    return 1.0 - exp(-d * d);
}
