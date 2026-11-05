#version 450
#extension GL_GOOGLE_include_directive : require
layout(set = 0, binding = 0) uniform sampler2D tDisplay;
layout(push_constant) uniform PC { vec4 a, b, c, d, e; } pc;
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;
#include "post_finish_common.glsl"
