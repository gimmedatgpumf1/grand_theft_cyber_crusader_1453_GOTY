#version 450

// The fragment stage of the Vulkan backend.
//
// It samples the material, converts the texel from sRGB to linear, applies the
// vertex-shaded colour, then tone-maps and encodes again -- the same Reinhard
// fit and the same piecewise transfer the OpenGL fragment shader applies, so
// the two backends produce the same picture rather than one looking washed out.
//
// The sampler is bound even for untextured batches: the renderer hands over a
// 1x1 white image, and flags.x says whether the result is used. That keeps one
// pipeline, one descriptor layout and one module for every material.

layout(location = 0) in vec4 inColour;
layout(location = 1) in vec2 inTexCoord;

layout(location = 0) out vec4 outColour;

layout(set = 1, binding = 0) uniform sampler2D baseMap;

layout(push_constant) uniform Batch {
    mat4 model;
    vec4 colour;
    vec4 flags;
} batch;

vec3 toLinear(vec3 value) {
    vec3 usePower = step(vec3(0.04045), value);
    return mix(value / 12.92, pow((value + 0.055) / 1.055, vec3(2.4)), usePower);
}

vec3 toSrgb(vec3 value) {
    vec3 usePower = step(vec3(0.0031308), value);
    return mix(value * 12.92, 1.055 * pow(value, vec3(1.0 / 2.4)) - 0.055, usePower);
}

vec3 toneMap(vec3 value) {
    return clamp((value * (2.51 * value + 0.03)) /
                 (value * (2.43 * value + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
    float textured = clamp(batch.flags.x, 0.0, 1.0);
    vec4 texel = texture(baseMap, inTexCoord);
    vec3 base = inColour.rgb * mix(vec3(1.0), toLinear(texel.rgb), textured);
    // An untextured batch keeps its own alpha; a textured one borrows the
    // material's, which is how the hair cut-out reads on both backends.
    float alpha = inColour.a * mix(1.0, texel.a, textured);
    outColour = vec4(toSrgb(toneMap(base)), alpha);
}
