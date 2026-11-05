#version 450
#extension GL_GOOGLE_include_directive : require
layout(set = 0, binding = 0) uniform sampler2D tScene;
layout(set = 0, binding = 1) uniform sampler2D tBloom;
layout(set = 0, binding = 2) uniform sampler2D tLut;
layout(set = 0, binding = 3) uniform sampler2D tHalation;
layout(set = 0, binding = 4) uniform sampler2D tSunVisibility;
layout(push_constant) uniform PC { vec4 a, b, c, d, e, f, g, h; } pc;
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;
#include "post_sun_flare.glsl"
#define SAKUGA_SUN_FLARE
#include "post_composite_common.glsl"
