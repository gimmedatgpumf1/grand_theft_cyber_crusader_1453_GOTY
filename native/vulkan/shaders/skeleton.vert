#version 450

// The vertex stage of the Vulkan backend.
//
// Positions arrive in world space, so the batch model matrix lifts them and the
// frame projection finishes the job. Lighting is Lambert against the same two
// lights the OpenGL path uses, with the same strengths, and it happens here
// rather than per fragment because the result is smooth enough to interpolate
// and it costs a third of the work.
//
// The flag word lets a material ask for flat colour -- the room's floor, walls
// and HUD quads -- the way `constantLighting` does on the CPU path.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec4 outColour;
layout(location = 1) out vec2 outTexCoord;

layout(set = 0, binding = 0) uniform Frame {
    mat4 projection;
    vec4 eye;
} frame;

layout(push_constant) uniform Batch {
    mat4 model;
    vec4 colour;
    vec4 flags;
} batch;

const vec3 KEY = normalize(vec3(-0.45, 0.78, -0.55));
const vec3 FILL = normalize(vec3(0.65, 0.25, -0.35));
const float KEY_STRENGTH = 0.78;
const float FILL_STRENGTH = 0.24;
const float AMBIENT = 0.38;

void main() {
    vec4 world = batch.model * vec4(inPosition, 1.0);
    gl_Position = frame.projection * world;
    outTexCoord = inTexCoord;

    // The normal takes the rotation part of the model matrix, which is what
    // model * vec4(normal, 0) does. flags.z says the matrix is rigid -- a bone
    // is a rotation and a translation -- in which case the result is already
    // unit length and the square root below is a million of them a frame spent
    // to change nothing.
    vec3 normal = mat3(batch.model) * inNormal;
    if (batch.flags.z < 0.5) normal = normalize(normal);

    // Two-sided lighting: a normal that points away from the eye is turned
    // round, exactly as the CPU rasterizer and the OpenGL shader do, so a back
    // face is lit rather than left black.
    vec3 toward = world.xyz - frame.eye.xyz;
    if (dot(normal, toward) > 0.0) normal = -normal;

    float diffuse = AMBIENT + KEY_STRENGTH * max(0.0, dot(normal, KEY)) +
                    FILL_STRENGTH * max(0.0, dot(normal, FILL));

    // flags.y is the "constant lighting" flag: one means do not shade.
    float flatShade = clamp(batch.flags.y, 0.0, 1.0);
    float factor = mix(diffuse, 1.0, flatShade);
    outColour = vec4(batch.colour.rgb * factor, batch.colour.a);
}
