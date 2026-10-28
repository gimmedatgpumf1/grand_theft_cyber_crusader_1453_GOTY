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

// A lookup table for the sRGB transfer curve, one row each way, indexed by
// channel value. pow() is one of the more expensive things a fragment shader can
// do and there were nine of them here per pixel -- six thousand million a frame
// at this size. The curve is fixed, so it is measured once on the host, and the
// shader does three filtered lookups instead.
layout(set = 0, binding = 1) uniform sampler2D transferLut;

layout(push_constant) uniform Batch {
    mat4 model;
    vec4 colour;
    vec4 flags;
} batch;

const float kDecodeRow = 0.25;  // row 0 of two: sRGB -> linear
const float kEncodeRow = 0.75;  // row 1 of two: linear -> sRGB

vec3 transfer(vec3 value, float row) {
    return vec3(texture(transferLut, vec2(clamp(value.r, 0.0, 1.0), row)).r,
                texture(transferLut, vec2(clamp(value.g, 0.0, 1.0), row)).r,
                texture(transferLut, vec2(clamp(value.b, 0.0, 1.0), row)).r);
}

vec3 toLinear(vec3 value) { return transfer(value, kDecodeRow); }

vec3 toSrgb(vec3 value) { return transfer(value, kEncodeRow); }

vec3 toneMap(vec3 value) {
    return clamp((value * (2.51 * value + 0.03)) /
                 (value * (2.43 * value + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
    const float textured = clamp(batch.flags.x, 0.0, 1.0);
    vec3 base;
    float alpha;
    if (textured > 0.0) {
        // An untextured batch keeps its own alpha; a textured one borrows the
        // material's, which is how the hair cut-out reads on both backends.
        const vec4 texel = texture(baseMap, inTexCoord);
        base = inColour.rgb * toLinear(texel.rgb);
        alpha = inColour.a * texel.a;
    } else {
        // A flat batch used to sample the material and then multiply the result
        // by zero: four filtered fetches a pixel, on every floor tile, every
        // wall and every bone, for nothing. The room and the whole skeleton are
        // drawn this way, so this is the branch that matters.
        base = inColour.rgb;
        alpha = inColour.a;
    }
    outColour = vec4(toSrgb(toneMap(base)), alpha);
}
