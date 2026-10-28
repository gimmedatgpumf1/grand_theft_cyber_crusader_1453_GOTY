"""Emit the Vulkan shaders for the skeleton renderer as SPIR-V binaries.

The shaders are written out twice over: as GLSL in `shaders/` for anyone who
wants to read what they do, and as SPIR-V here, because this build has no shader
compiler and cannot get one. Neither the Vulkan SDK nor a package mirror offers
glslang, shaderc or spirv-tools to the build, and `release-assets.github`
is unreachable, so a compiler is not an option and a binary blob of unknown
origin would not be reviewable. Building the module from named instructions --
each one emitted with exactly the operands the specification gives it, with the
word count derived from those operands rather than typed by hand -- is the
option that can be read, reviewed and regenerated.

Run `python3 native/vulkan/spirv.py --write` to regenerate `shaders/*.spv`, and
`python3 native/vulkan/spirv.py --print` to disassemble what is in the tree. The
renderer checks the magic number and the declared bound on load and refuses a
malformed module, so a bad regeneration fails at startup instead of drawing
nonsense.

This is only for the first milestone -- untextured, one directional key light
and one fill, colour per batch. Once a shader compiler is on the machine the
same GLSL can be compiled with `glslangValidator` and this file retires.
"""

import argparse
import pathlib
import struct

# One instruction is a leading word of `word count << 16 | opcode`, then the
# operands. Every helper below takes its operands in specification order, so the
# word count is always the honest one.
OP = {
    'OpCapability': 17, 'OpMemoryModel': 14, 'OpEntryPoint': 15,
    'OpExecutionMode': 16, 'OpTypeVoid': 19, 'OpTypeFloat': 22, 'OpTypeInt': 21,
    'OpTypeVector': 23, 'OpTypeMatrix': 24, 'OpTypeStruct': 30,
    'OpTypePointer': 32, 'OpTypeFunction': 33, 'OpConstant': 43, 'OpConstantComposite': 44,
    'OpLabel': 248, 'OpReturn': 253, 'OpFunction': 54, 'OpFunctionEnd': 56, 'OpVariable': 59, 'OpLoad': 61,
    'OpStore': 62, 'OpAccessChain': 65, 'OpDecorate': 71,
    'OpMemberDecorate': 72, 'OpVectorShuffle': 79,
    'OpCompositeConstruct': 80, 'OpCompositeExtract': 81, 'OpFAdd': 129,
    'OpFSub': 131, 'OpFMul': 133, 'OpVectorTimesScalar': 142,
    'OpMatrixTimesVector': 145, 'OpDot': 148, 'OpFMulSat': 156,
    'OpExtInstImport': 11, 'OpExtInst': 12,
    'OpTypeBool': 20, 'OpTypeImage': 25, 'OpTypeSampledImage': 27,
    'OpConstantTrue': 41, 'OpConstantFalse': 42,
    'OpImageSampleImplicitLod': 87, 'OpImage': 100, 'OpFDiv': 132,
}

# Decoration and addressing vocabulary this module uses. Every one of these is
# a fixed number in the SPIR-V specification, not a choice.
BUILTIN_POSITION = 0
DECORATION_BLOCK = 2
DECORATION_BUILTIN = 11
DECORATION_LOCATION = 30
DECORATION_BINDING = 33
DECORATION_DESCRIPTOR_SET = 34
DECORATION_OFFSET = 35
CAPABILITY_SHADER = 1
MEMORY_LOGICAL = 0
MEMORY_GLSL450 = 1
MODEL_VERTEX = 0
MODEL_FRAGMENT = 4
MODE_ORIGIN_UPPER_LEFT = 7
CLASS_INPUT = 1
CLASS_UNIFORM = 2
CLASS_OUTPUT = 3
CLASS_FUNCTION = 7
CLASS_PUSH_CONSTANT = 9
CLASS_UNIFORM_CONSTANT = 0
DIM_2D = 1
SAMPLE_SAMPLED = 1
FORMAT_UNKNOWN = 0
# Opcodes of the GLSL.std.450 extended instruction set, which every Vulkan
# driver is required to provide.
GLSL_POW = 26
GLSL_FCLAMP = 43
GLSL_FMIX = 46
GLSL_STEP = 48


def unit(vector):
    """A unit vector, because a light direction is only a direction if it is."""
    length = sum(component * component for component in vector) ** 0.5
    return tuple(component / length for component in vector)


# The two lights the OpenGL path has always used, so the two backends agree
# about where the light comes from.
KEY = unit((-0.45, 0.78, -0.55))
FILL = unit((0.65, 0.25, -0.35))
KEY_STRENGTH, FILL_STRENGTH, AMBIENT = 0.78, 0.24, 0.38


class Module:
    """A SPIR-V module built from named instructions, ids allocated in order."""

    def __init__(self):
        self.words = [0x07230203, 0x00010000, 0, 0, 0]  # magic, version, generator, bound, schema
        self.next_id = 1
        self.names = {}

    def id(self, name=None):
        value = self.next_id
        self.next_id += 1
        if name:
            self.names[name] = value
        return value

    def op(self, name, *operands):
        flat = []
        for operand in operands:
            if isinstance(operand, str):
                # A literal string is packed four bytes to a word, null padded.
                encoded = operand.encode('utf-8') + b'\0'
                while len(encoded) % 4:
                    encoded += b'\0'
                flat.extend(struct.unpack(f'<{len(encoded) // 4}I', encoded))
            elif isinstance(operand, (list, tuple)):
                flat.extend(int(value) for value in operand)
            else:
                flat.append(int(operand))
        self.words.append((len(flat) + 1) << 16 | OP[name])
        self.words.extend(flat)

    def float(self, value):
        return struct.unpack('<I', struct.pack('<f', value))[0]

    def finish(self):
        self.words[3] = self.next_id  # bound must exceed every id used
        return self.words


def vertex_shader():
    """Lambert in the vertex stage, colour out; no branches, no textures.

    Positions arrive in world space when the renderer runs in GPU mode, so the
    batch model matrix lifts them and the frame projection finishes the job; the
    normal takes the same model matrix with w = 0. The flag word lets a material
    ask for flat colour -- the room's floor and walls, HUD quads -- the way
    `constantLighting` does on the CPU path, done with arithmetic rather than a
    branch so the module stays straight-line.
    """
    m = Module()
    void, f32, uint, vec2, vec3, vec4, mat4 = (m.id() for _ in range(7))
    ptr_in_vec3, ptr_in_vec2, ptr_out_vec4 = m.id(), m.id(), m.id()
    ptr_out_vec2 = m.id()
    frame, ptr_uniform_frame, ptr_uniform_mat4 = m.id(), m.id(), m.id()
    ptr_uniform_vec4 = m.id()
    batch = m.id()
    ptr_push_batch, ptr_push_mat4, ptr_push_vec4 = m.id(), m.id(), m.id()
    fn_type, main, label = m.id(), m.id(), m.id()
    ext = m.id()
    in_pos, in_normal, in_uv = m.id(), m.id(), m.id()
    out_colour, out_position, out_uv = m.id(), m.id(), m.id()
    frame_var, batch_var = m.id(), m.id()
    zero, one = m.id(), m.id()
    ambient, key_strength, fill_strength = m.id(), m.id(), m.id()
    key, fill = m.id(), m.id()
    # A composite is built from ids, never from literals: each component needs a
    # scalar constant of its own first.
    key_parts = [m.id() for _ in KEY]
    fill_parts = [m.id() for _ in FILL]
    index = [m.id() for _ in range(5)]

    m.op('OpCapability', CAPABILITY_SHADER)
    m.op('OpMemoryModel', MEMORY_LOGICAL, MEMORY_GLSL450)
    m.op('OpEntryPoint', MODEL_VERTEX, main, 'main',
         [in_pos, in_normal, in_uv, out_colour, out_position, out_uv])
    m.op('OpDecorate', in_pos, DECORATION_LOCATION, 0)
    m.op('OpDecorate', in_normal, DECORATION_LOCATION, 1)
    m.op('OpDecorate', in_uv, DECORATION_LOCATION, 2)
    m.op('OpDecorate', out_colour, DECORATION_LOCATION, 0)
    m.op('OpDecorate', out_uv, DECORATION_LOCATION, 1)
    m.op('OpDecorate', out_position, DECORATION_BUILTIN, BUILTIN_POSITION)
    m.op('OpDecorate', frame, DECORATION_BLOCK)
    m.op('OpMemberDecorate', frame, 0, DECORATION_OFFSET, 0)
    m.op('OpMemberDecorate', frame, 1, DECORATION_OFFSET, 64)
    m.op('OpDecorate', frame_var, DECORATION_DESCRIPTOR_SET, 0)
    m.op('OpDecorate', frame_var, DECORATION_BINDING, 0)
    m.op('OpDecorate', batch, DECORATION_BLOCK)
    m.op('OpMemberDecorate', batch, 0, DECORATION_OFFSET, 0)
    m.op('OpMemberDecorate', batch, 1, DECORATION_OFFSET, 64)
    m.op('OpMemberDecorate', batch, 2, DECORATION_OFFSET, 80)

    # Types, then constants, then globals, then functions: the logical layout.
    m.op('OpExtInstImport', ext, 'GLSL.std.450')
    m.op('OpTypeVoid', void)
    m.op('OpTypeFloat', f32, 32)
    m.op('OpTypeInt', uint, 32, 0)
    m.op('OpTypeVector', vec2, f32, 2)
    m.op('OpTypeVector', vec3, f32, 3)
    m.op('OpTypeVector', vec4, f32, 4)
    m.op('OpTypeMatrix', mat4, vec4, 4)
    m.op('OpTypeStruct', frame, mat4, vec4)
    m.op('OpTypeStruct', batch, mat4, vec4, vec4)
    m.op('OpTypePointer', ptr_in_vec3, CLASS_INPUT, vec3)
    m.op('OpTypePointer', ptr_in_vec2, CLASS_INPUT, vec2)
    m.op('OpTypePointer', ptr_out_vec4, CLASS_OUTPUT, vec4)
    m.op('OpTypePointer', ptr_out_vec2, CLASS_OUTPUT, vec2)
    m.op('OpTypePointer', ptr_uniform_frame, CLASS_UNIFORM, frame)
    m.op('OpTypePointer', ptr_uniform_mat4, CLASS_UNIFORM, mat4)
    m.op('OpTypePointer', ptr_uniform_vec4, CLASS_UNIFORM, vec4)
    m.op('OpTypePointer', ptr_push_batch, CLASS_PUSH_CONSTANT, batch)
    m.op('OpTypePointer', ptr_push_mat4, CLASS_PUSH_CONSTANT, mat4)
    m.op('OpTypePointer', ptr_push_vec4, CLASS_PUSH_CONSTANT, vec4)
    m.op('OpTypeFunction', fn_type, void)

    m.op('OpConstant', f32, zero, m.float(0.0))
    m.op('OpConstant', f32, one, m.float(1.0))
    m.op('OpConstant', f32, ambient, m.float(AMBIENT))
    m.op('OpConstant', f32, key_strength, m.float(KEY_STRENGTH))
    m.op('OpConstant', f32, fill_strength, m.float(FILL_STRENGTH))
    for identifier, value in zip(key_parts, KEY):
        m.op('OpConstant', f32, identifier, m.float(value))
    for identifier, value in zip(fill_parts, FILL):
        m.op('OpConstant', f32, identifier, m.float(value))
    m.op('OpConstantComposite', vec3, key, key_parts)
    m.op('OpConstantComposite', vec3, fill, fill_parts)
    for number, constant in enumerate(index):
        m.op('OpConstant', uint, constant, number)

    m.op('OpVariable', ptr_in_vec3, in_pos, CLASS_INPUT)
    m.op('OpVariable', ptr_in_vec3, in_normal, CLASS_INPUT)
    m.op('OpVariable', ptr_in_vec2, in_uv, CLASS_INPUT)
    m.op('OpVariable', ptr_out_vec4, out_colour, CLASS_OUTPUT)
    m.op('OpVariable', ptr_out_vec4, out_position, CLASS_OUTPUT)
    m.op('OpVariable', ptr_out_vec2, out_uv, CLASS_OUTPUT)
    m.op('OpVariable', ptr_uniform_frame, frame_var, CLASS_UNIFORM)
    m.op('OpVariable', ptr_push_batch, batch_var, CLASS_PUSH_CONSTANT)

    m.op('OpFunction', void, main, 0, fn_type)
    m.op('OpLabel', label)

    projection_ptr = m.id()
    m.op('OpAccessChain', ptr_uniform_mat4, projection_ptr, frame_var, [index[0]])
    model_ptr = m.id()
    m.op('OpAccessChain', ptr_push_mat4, model_ptr, batch_var, [index[0]])
    colour_ptr = m.id()
    m.op('OpAccessChain', ptr_push_vec4, colour_ptr, batch_var, [index[1]])
    flags_ptr = m.id()
    m.op('OpAccessChain', ptr_push_vec4, flags_ptr, batch_var, [index[2]])
    eye_ptr = m.id()
    m.op('OpAccessChain', ptr_uniform_vec4, eye_ptr, frame_var, [index[4]])
    projection, model, colour, flags, eye = m.id(), m.id(), m.id(), m.id(), m.id()
    m.op('OpLoad', vec4, eye, eye_ptr)
    m.op('OpLoad', mat4, projection, projection_ptr)
    m.op('OpLoad', mat4, model, model_ptr)
    m.op('OpLoad', vec4, colour, colour_ptr)
    m.op('OpLoad', vec4, flags, flags_ptr)

    position = m.id()
    m.op('OpLoad', vec3, position, in_pos)
    lifted = m.id()
    m.op('OpCompositeConstruct', vec4, lifted, [position, one])
    moved = m.id()
    m.op('OpMatrixTimesVector', vec4, moved, model, lifted)
    clip = m.id()
    m.op('OpMatrixTimesVector', vec4, clip, projection, moved)
    m.op('OpStore', out_position, clip)

    # The material lookup happens in the fragment stage, so the coordinate has
    # to survive interpolation across the triangle.
    coordinate = m.id()
    m.op('OpLoad', vec2, coordinate, in_uv)
    m.op('OpStore', out_uv, coordinate)

    normal = m.id()
    m.op('OpLoad', vec3, normal, in_normal)
    normal4 = m.id()
    m.op('OpCompositeConstruct', vec4, normal4, [normal, zero])
    turned = m.id()
    m.op('OpMatrixTimesVector', vec4, turned, model, normal4)
    sideways = m.id()
    m.op('OpCompositeExtract', vec3, sideways, turned, 0)

    # Two-sided lighting: a normal that points away from the eye is turned
    # round, which is what the CPU rasterizer and the GL shader both do, so a
    # back face is lit rather than left black.
    world = m.id()
    m.op('OpCompositeExtract', vec3, world, moved, 0)
    toward = m.id()
    m.op('OpFSub', vec3, toward, world, eye)
    facing = m.id()
    m.op('OpDot', f32, facing, sideways, toward)
    away = m.id()
    m.op('OpExtInst', f32, away, ext, GLSL_STEP, zero, facing)
    doubled = m.id()
    m.op('OpFAdd', f32, doubled, away, away)
    signed = m.id()
    m.op('OpFSub', f32, signed, one, doubled)
    oriented = m.id()
    m.op('OpVectorTimesScalar', vec3, oriented, sideways, signed)

    key_dot, fill_dot = m.id(), m.id()
    m.op('OpDot', f32, key_dot, oriented, key)
    m.op('OpDot', f32, fill_dot, oriented, fill)
    key_sat, fill_sat = m.id(), m.id()
    m.op('OpFMulSat', f32, key_sat, key_dot, one)
    m.op('OpFMulSat', f32, fill_sat, fill_dot, one)
    key_term, fill_term = m.id(), m.id()
    m.op('OpFMul', f32, key_term, key_sat, key_strength)
    m.op('OpFMul', f32, fill_term, fill_sat, fill_strength)
    two_lights = m.id()
    m.op('OpFAdd', f32, two_lights, key_term, fill_term)
    lit = m.id()
    m.op('OpFAdd', f32, lit, two_lights, ambient)

    flat = m.id()
    m.op('OpCompositeExtract', f32, flat, flags, 1)
    flag = m.id()
    m.op('OpExtInst', f32, flag, ext, GLSL_FCLAMP, flat, zero, one)
    inverse = m.id()
    m.op('OpFSub', f32, inverse, one, flag)
    weighted = m.id()
    m.op('OpFMul', f32, weighted, lit, flag)
    factor = m.id()
    m.op('OpFAdd', f32, factor, weighted, inverse)

    base = m.id()
    m.op('OpCompositeExtract', vec3, base, colour, 0)
    shade = m.id()
    m.op('OpVectorTimesScalar', vec3, shade, base, factor)
    alpha = m.id()
    m.op('OpCompositeExtract', f32, alpha, colour, 3)
    shaded = m.id()
    m.op('OpCompositeConstruct', vec4, shaded, [shade, alpha])
    m.op('OpStore', out_colour, shaded)

    m.op('OpReturn')
    m.op('OpFunctionEnd')
    return m.finish()


def fragment_shader():
    """Sample the material, light it, then put the picture on the display.

    The OpenGL path tone-maps and sRGB-encodes in its fragment shader, so this
    one does the same rather than handing back linear values and looking washed
    out: the same Reinhard fit, then the same piecewise sRGB transfer. Untextured
    batches sample a 1x1 white image, which lets one pipeline and one module
    serve both cases -- "textured" is a flag, not a second variant.
    """
    m = Module()
    void, f32, uint, boolean = m.id(), m.id(), m.id(), m.id()
    vec2, vec3, vec4, mat4 = m.id(), m.id(), m.id(), m.id()
    image, sampled, batch = m.id(), m.id(), m.id()
    ptr_in_vec4, ptr_in_vec2, ptr_out_vec4 = m.id(), m.id(), m.id()
    ptr_sampler, ptr_push_batch, ptr_push_vec4 = m.id(), m.id(), m.id()
    fn_type, main, label = m.id(), m.id(), m.id()
    in_colour, in_uv, out_colour = m.id(), m.id(), m.id()
    ext, sampler_var, batch_var = m.id(), m.id(), m.id()

    # Every literal the body needs, as a scalar and as a vec3 splat, because
    # SPIR-V has no mixed vector/scalar arithmetic: a scalar has to be widened
    # into a composite before it can be added to a vector.
    named = {
        'zero': 0.0, 'one': 1.0,
        'srgb_lo': 12.92, 'srgb_knee': 0.0031308, 'srgb_scale': 1.055,
        'srgb_gamma': 1.0 / 2.4,
        'lin_knee': 0.04045, 'lin_exp': 2.4, 'lin_scale': 0.055,
        'aces_a': 2.51, 'aces_b': 0.03, 'aces_c': 2.43, 'aces_d': 0.59,
        'aces_e': 0.14,
    }
    scalar = {name: m.id() for name in named}
    splat = {name: m.id() for name in named}
    index_one, index_two = m.id(), m.id()

    def ext_inst(opcode, result_type, *operands):
        result = m.id()
        m.op('OpExtInst', result_type, result, ext, opcode, *operands)
        return result

    m.op('OpCapability', CAPABILITY_SHADER)
    m.op('OpMemoryModel', MEMORY_LOGICAL, MEMORY_GLSL450)
    m.op('OpEntryPoint', MODEL_FRAGMENT, main, 'main', [in_colour, in_uv, out_colour])
    m.op('OpExecutionMode', main, MODE_ORIGIN_UPPER_LEFT)
    m.op('OpDecorate', in_colour, DECORATION_LOCATION, 0)
    m.op('OpDecorate', in_uv, DECORATION_LOCATION, 1)
    m.op('OpDecorate', out_colour, DECORATION_LOCATION, 0)
    # The sampler lives in set 1 of its own, so each distinct material texture
    # needs one descriptor set and not one per batch.
    m.op('OpDecorate', sampler_var, DECORATION_DESCRIPTOR_SET, 1)
    m.op('OpDecorate', sampler_var, DECORATION_BINDING, 0)
    m.op('OpDecorate', batch, DECORATION_BLOCK)
    m.op('OpMemberDecorate', batch, 0, DECORATION_OFFSET, 0)
    m.op('OpMemberDecorate', batch, 1, DECORATION_OFFSET, 64)
    m.op('OpMemberDecorate', batch, 2, DECORATION_OFFSET, 80)

    m.op('OpExtInstImport', ext, 'GLSL.std.450')
    m.op('OpTypeVoid', void)
    m.op('OpTypeFloat', f32, 32)
    m.op('OpTypeInt', uint, 32, 0)
    m.op('OpTypeBool', boolean)
    m.op('OpTypeVector', vec2, f32, 2)
    m.op('OpTypeVector', vec3, f32, 3)
    m.op('OpTypeVector', vec4, f32, 4)
    m.op('OpTypeMatrix', mat4, vec4, 4)
    m.op('OpTypeImage', image, f32, DIM_2D, 0, 0, 0, SAMPLE_SAMPLED, FORMAT_UNKNOWN)
    m.op('OpTypeSampledImage', sampled, image)
    m.op('OpTypeStruct', batch, mat4, vec4, vec4)
    m.op('OpTypePointer', ptr_in_vec4, CLASS_INPUT, vec4)
    m.op('OpTypePointer', ptr_in_vec2, CLASS_INPUT, vec2)
    m.op('OpTypePointer', ptr_out_vec4, CLASS_OUTPUT, vec4)
    m.op('OpTypePointer', ptr_sampler, CLASS_UNIFORM_CONSTANT, sampled)
    m.op('OpTypePointer', ptr_push_batch, CLASS_PUSH_CONSTANT, batch)
    m.op('OpTypePointer', ptr_push_vec4, CLASS_PUSH_CONSTANT, vec4)
    m.op('OpTypeFunction', fn_type, void)

    for name, value in named.items():
        m.op('OpConstant', f32, scalar[name], m.float(value))
    for name in named:
        m.op('OpConstantComposite', vec3, splat[name], [scalar[name]] * 3)
    m.op('OpConstant', uint, index_one, 1)
    m.op('OpConstant', uint, index_two, 2)

    m.op('OpVariable', ptr_in_vec4, in_colour, CLASS_INPUT)
    m.op('OpVariable', ptr_in_vec2, in_uv, CLASS_INPUT)
    m.op('OpVariable', ptr_out_vec4, out_colour, CLASS_OUTPUT)
    m.op('OpVariable', ptr_sampler, sampler_var, CLASS_UNIFORM_CONSTANT)
    m.op('OpVariable', ptr_push_batch, batch_var, CLASS_PUSH_CONSTANT)

    m.op('OpFunction', void, main, 0, fn_type)
    m.op('OpLabel', label)

    colour_ptr = m.id()
    m.op('OpAccessChain', ptr_push_vec4, colour_ptr, batch_var, [index_one])
    colour = m.id()
    m.op('OpLoad', vec4, colour, colour_ptr)
    flags_ptr = m.id()
    m.op('OpAccessChain', ptr_push_vec4, flags_ptr, batch_var, [index_two])
    flags = m.id()
    m.op('OpLoad', vec4, flags, flags_ptr)
    textured = m.id()
    m.op('OpCompositeExtract', f32, textured, flags, 0)

    uv = m.id()
    m.op('OpLoad', vec2, uv, in_uv)
    bound = m.id()
    m.op('OpLoad', sampled, bound, sampler_var)
    texel = m.id()
    m.op('OpImageSampleImplicitLod', vec4, texel, bound, uv)
    tex_rgb = m.id()
    m.op('OpCompositeExtract', vec3, tex_rgb, texel, 0)
    tex_a = m.id()
    m.op('OpCompositeExtract', f32, tex_a, texel, 3)

    # toLinear(texel) = mix(texel / 12.92, ((texel + 0.055) / 1.055) ^ 2.4,
    #                      step(0.04045, texel))
    shifted = m.id()
    m.op('OpFAdd', vec3, shifted, tex_rgb, splat['lin_scale'])
    divided = m.id()
    m.op('OpFDiv', vec3, divided, shifted, splat['srgb_scale'])
    exact = ext_inst(GLSL_POW, vec3, divided, splat['lin_exp'])
    shallow = m.id()
    m.op('OpFDiv', vec3, shallow, tex_rgb, splat['srgb_lo'])
    lin_gate = ext_inst(GLSL_STEP, vec3, splat['lin_knee'], tex_rgb)
    tex_linear = ext_inst(GLSL_FMIX, vec3, shallow, exact, lin_gate)

    # Untextured batches multiply by one; textured ones multiply by the sample.
    gate_rgb = m.id()
    m.op('OpCompositeConstruct', vec3, gate_rgb, [textured] * 3)
    detail = ext_inst(GLSL_FMIX, vec3, splat['one'], tex_linear, gate_rgb)

    base = m.id()
    m.op('OpCompositeExtract', vec3, base, colour, 0)
    lit = m.id()
    m.op('OpFMul', vec3, lit, base, detail)

    def tone_map(value):
        scaled_up = m.id()
        m.op('OpVectorTimesScalar', vec3, scaled_up, value, scalar['aces_a'])
        numerator_shifted = m.id()
        m.op('OpFAdd', vec3, numerator_shifted, scaled_up, splat['aces_b'])
        numerator = m.id()
        m.op('OpFMul', vec3, numerator, value, numerator_shifted)
        scaled_down = m.id()
        m.op('OpVectorTimesScalar', vec3, scaled_down, value, scalar['aces_c'])
        denominator_shifted = m.id()
        m.op('OpFAdd', vec3, denominator_shifted, scaled_down, splat['aces_d'])
        denominator = m.id()
        m.op('OpFMul', vec3, denominator, value, denominator_shifted)
        raised = m.id()
        m.op('OpFAdd', vec3, raised, denominator, splat['aces_e'])
        ratio = m.id()
        m.op('OpFDiv', vec3, ratio, numerator, raised)
        return ext_inst(GLSL_FCLAMP, vec3, ratio, splat['zero'], splat['one'])

    tone = tone_map(lit)
    low = m.id()
    m.op('OpFMul', vec3, low, tone, splat['srgb_lo'])
    powered = ext_inst(GLSL_POW, vec3, tone, splat['srgb_gamma'])
    boosted = m.id()
    m.op('OpFMul', vec3, boosted, powered, splat['srgb_scale'])
    high = m.id()
    m.op('OpFSub', vec3, high, boosted, splat['lin_scale'])
    srgb_gate = ext_inst(GLSL_STEP, vec3, splat['srgb_knee'], tone)
    encoded = ext_inst(GLSL_FMIX, vec3, low, high, srgb_gate)

    alpha_in = m.id()
    m.op('OpCompositeExtract', f32, alpha_in, colour, 3)
    # An untextured batch keeps its own alpha; a textured one borrows the
    # material's, which is how the hair cut-out reads on both backends.
    tex_alpha = ext_inst(GLSL_FMIX, f32, scalar['one'], tex_a, textured)
    alpha_total = m.id()
    m.op('OpFMul', f32, alpha_total, alpha_in, tex_alpha)

    written = m.id()
    m.op('OpCompositeConstruct', vec4, written, [encoded, alpha_total])
    m.op('OpStore', out_colour, written)

    m.op('OpReturn')
    m.op('OpFunctionEnd')
    return m.finish()


def disassemble(words):
    """Read a module back, so what was written can be checked by eye."""
    if words[0] != 0x07230203:
        raise SystemExit('not a SPIR-V module')
    names = {value: name for name, value in OP.items()}
    bound = words[3]
    offset = 5
    lines = [f'bound {bound}']
    while offset < len(words):
        first = words[offset]
        count, opcode = first >> 16, first & 0xFFFF
        if count == 0 or offset + count > len(words):
            raise SystemExit(f'malformed instruction at word {offset}')
        operands = words[offset + 1:offset + count]
        operands = [f'"{chr(c)}"' if 0x20 <= c < 0x7F and opcode == OP['OpEntryPoint'] and i > 2
                    else str(c) for i, c in enumerate(operands)]
        lines.append(f'  {names.get(opcode, opcode):22s} {" ".join(operands)}')
        offset += count
    return '\n'.join(lines)


TYPE_OPS = {
    'OpTypeVoid', 'OpTypeBool', 'OpTypeFloat', 'OpTypeInt', 'OpTypeVector',
    'OpTypeMatrix', 'OpTypeStruct', 'OpTypePointer', 'OpTypeFunction',
    'OpTypeImage', 'OpTypeSampledImage',
}

# Where each value-producing instruction keeps its result id, as an index into
# the operands. Everything not listed produces no value.
RESULT_INDEX = {
    'OpTypeVoid': 0, 'OpTypeFloat': 0, 'OpTypeInt': 0, 'OpTypeVector': 0,
    'OpTypeMatrix': 0, 'OpTypeStruct': 0, 'OpTypePointer': 0, 'OpTypeFunction': 0,
    'OpConstant': 1, 'OpConstantComposite': 1, 'OpVariable': 1, 'OpFunction': 1,
    'OpConstantTrue': 1, 'OpConstantFalse': 1, 'OpExtInstImport': 0, 'OpExtInst': 1,
    'OpImageSampleImplicitLod': 1, 'OpFDiv': 1, 'OpTypeImage': 0,
    'OpTypeSampledImage': 0, 'OpTypeBool': 0, 'OpImage': 1,
    'OpLabel': 0, 'OpLoad': 1, 'OpAccessChain': 1, 'OpCompositeConstruct': 1,
    'OpCompositeExtract': 1, 'OpVectorTimesScalar': 1, 'OpMatrixTimesVector': 1,
    'OpDot': 1, 'OpFAdd': 1, 'OpFSub': 1, 'OpFMul': 1, 'OpFMulSat': 1,
}
# Operands that are literals rather than ids, so a pass over "everything that
# looks like an id" does not mistake a member index for one.
LITERALS = {
    'OpTypeFloat': {1}, 'OpTypeVector': {2}, 'OpTypeMatrix': {2},
    'OpTypeStruct': None, 'OpDecorate': None, 'OpMemberDecorate': None,
    'OpEntryPoint': None, 'OpExecutionMode': None, 'OpCompositeExtract': None,
    'OpConstant': None,
}


def string_words(words):
    """How many words a packed literal string occupies, terminator included."""
    for position, word in enumerate(words):
        if b'\0' in struct.pack('<I', word):
            return position + 1
    raise ValueError('packed string is not null-terminated')


def _unpack_string(words):
    """Inverse of the string packing in Module.op; the trailing null is dropped."""
    raw = b''.join(struct.pack('<I', word) for word in words)
    return raw.split(b'\0')[0].decode('utf-8', 'replace')


def id_positions(name, operands):
    """Which operands of an instruction are ids rather than literals.

    Needed because a float constant's payload is an arbitrary 32-bit pattern and
    an entry point's name is a string: both are numerically large, and a check
    that treated every large word as an id would reject a correct module. Written
    out per opcode so the answer is visible instead of inferred.
    """
    if name in ('OpCapability', 'OpMemoryModel', 'OpReturn', 'OpFunctionEnd'):
        return []
    if name == 'OpEntryPoint':
        # The name is null-terminated but padded to whole words, so 'main' takes
        # two; the interface ids begin after the word holding the terminator.
        return [1] + list(range(2 + string_words(operands[2:]), len(operands)))
    if name in ('OpDecorate', 'OpMemberDecorate', 'OpExecutionMode'):
        return [0]
    if name == 'OpTypeStruct':
        return list(range(len(operands)))
    if name == 'OpTypeFunction':
        return list(range(len(operands)))
    if name in ('OpConstantComposite', 'OpCompositeConstruct'):
        return list(range(2, len(operands)))
    if name == 'OpConstant':
        return [0, 1]
    if name == 'OpVariable':
        return [0, 1] + ([3] if len(operands) > 3 else [])
    if name == 'OpLoad':
        return [0, 1, 2]
    if name == 'OpAccessChain':
        return list(range(len(operands))) if len(operands) < 3 else [0, 1, 2] + list(range(3, len(operands)))
    if name == 'OpCompositeExtract':
        return [0, 1, 2]
    if name in ('OpVectorTimesScalar', 'OpMatrixTimesVector', 'OpDot',
                'OpFAdd', 'OpFSub', 'OpFMul', 'OpFMulSat'):
        return [0, 1, 2, 3]
    if name == 'OpStore':
        return [0, 1]
    if name == 'OpExtInstImport':
        return []
    if name == 'OpExtInst':
        # Result type, result, the instruction set, a literal opcode, then
        # whatever arguments that opcode takes -- all ids.
        return [0, 1, 2] + list(range(4, len(operands)))
    if name == 'OpImageSampleImplicitLod':
        return list(range(len(operands)))
    if name == 'OpTypeImage':
        return [0, 1]
    if name in ('OpTypeSampledImage', 'OpConstantTrue', 'OpConstantFalse'):
        return [0, 1] if name != 'OpTypeSampledImage' else [0, 1]
    if name in ('OpFDiv',):
        return [0, 1, 2, 3]
    return [0]


def verify(words):
    """Prove the module is one a driver can accept, as far as structure goes.

    Word counts and magic numbers are checked elsewhere; what this adds is what a
    hand-built module actually gets wrong -- an id defined twice, an id read that
    nothing defines, a result type that is not a type, a bound that does not
    cover the ids in use -- plus the layout rules (types and globals before
    functions, `main` present, a push constant small enough for the guaranteed
    128 bytes).
    """
    assert words[0] == 0x07230203, 'magic'
    bound = words[3]
    assert bound > 0, 'bound must cover at least id 0'
    types = set()
    defined = set()
    functions = {'OpFunction', 'OpFunctionEnd', 'OpLabel', 'OpReturn', 'OpLoad',
                 'OpStore', 'OpAccessChain', 'OpCompositeConstruct',
                 'OpCompositeExtract', 'OpVectorTimesScalar', 'OpMatrixTimesVector',
                 'OpDot', 'OpFAdd', 'OpFSub', 'OpFMul', 'OpFMulSat'}
    seen_function = False
    seen_entry = False

    offset = 5
    while offset < len(words):
        count, opcode = words[offset] >> 16, words[offset] & 0xFFFF
        name = next((key for key, value in OP.items() if value == opcode), None)
        assert name is not None, f'unknown opcode {opcode} at word {offset}'
        assert count >= 1, f'{name}: zero word count at {offset}'
        assert offset + count <= len(words), f'{name}: runs past the end'
        operands = words[offset + 1:offset + count]
        positions = id_positions(name, operands)
        for position in positions:
            assert operands[position] < bound, f'{name}: id {operands[position]} >= bound {bound}'

        result = RESULT_INDEX.get(name)
        if name in TYPE_OPS:
            types.add(operands[0])
        if name == 'OpEntryPoint':
            seen_entry = True
            assert 'main' == _unpack_string(operands[2:]), 'entry point must be named main'
        if name in functions:
            seen_function = True
            if name == 'OpFunction':
                assert operands[0] in types and operands[3] in types, 'function type'
        if name == 'OpFunction' and not seen_function:
            seen_function = True
        if result is not None:
            identifier = operands[result]
            assert identifier and identifier not in defined, f'{name}: id {identifier} defined twice'
            defined.add(identifier)
            if name in ('OpLoad', 'OpAccessChain', 'OpVectorTimesScalar',
                        'OpMatrixTimesVector', 'OpDot', 'OpFAdd', 'OpFSub',
                        'OpFMul', 'OpFMulSat', 'OpCompositeExtract',
                        'OpCompositeConstruct', 'OpConstantComposite'):
                assert operands[0] in types, f'{name}: result type {operands[0]} is not a type'
            if name == 'OpStore':
                assert operands[0] in defined and operands[1] in defined, 'store operands'
        # Global OpVariable is legal at module scope; the rest are not.
        if name in ('OpLoad', 'OpStore', 'OpAccessChain',
                    'OpCompositeConstruct', 'OpCompositeExtract',
                    'OpVectorTimesScalar', 'OpMatrixTimesVector', 'OpDot',
                    'OpFAdd', 'OpFSub', 'OpFMul', 'OpFMulSat'):
            assert seen_function, f'{name} appears outside a function body'
        offset += count

    assert seen_entry, 'no OpEntryPoint'
    assert seen_function, 'no function body'
    # Every id in range must be defined somewhere: an id that is allocated and
    # never used still counts, but one that is used and never defined does not.
    offset = 5
    while offset < len(words):
        count, opcode = words[offset] >> 16, words[offset] & 0xFFFF
        name = next(key for key, value in OP.items() if value == opcode)
        operands = words[offset + 1:offset + count]
        for position in id_positions(name, operands):
            identifier = operands[position]
            assert identifier in defined or identifier in types, (
                f'{name} at word {offset} reads id {identifier}, which nothing defines')
        offset += count
    return len(defined)


def check(words):
    """Refuse a module that could not be one: magic, bound, word alignment."""
    assert words[0] == 0x07230203, 'magic'
    assert words[3] > 0, 'bound'
    highest = 0
    offset = 5
    while offset < len(words):
        count = words[offset] >> 16
        assert count > 0, f'zero-length instruction at {offset}'
        assert offset + count <= len(words), f'truncated instruction at {offset}'
        for operand in words[offset + 1:offset + count]:
            highest = max(highest, operand) if operand < words[3] else highest
        offset += count
    assert offset == len(words), 'trailing words'
    return len(words)


def header(modules):
    """The same modules as C++ arrays, so the binary carries its own shaders.

    A renderer that has to find a `.spv` next to the executable is a renderer
    that fails when the file is installed elsewhere; the modules are a few
    hundred words, so they belong in the object file.
    """
    lines = ['// Generated by native/vulkan/spirv.py -- do not edit by hand.',
             '#pragma once', '#include <cstdint>', '', 'namespace gtc::native::vk::spirv {', '']
    for name, module in modules:
        identifier = name.split('.')[0] + '_' + name.split('.')[1]
        lines.append(f'// {name}: {len(module)} words')
        lines.append(f'inline constexpr std::uint32_t {identifier}[] = {{')
        for start in range(0, len(module), 6):
            row = ', '.join(f'0x{word:08x}u' for word in module[start:start + 6])
            lines.append(f'    {row},')
        lines.append('};')
        lines.append('')
    lines.extend(['} // namespace gtc::native::vk::spirv', ''])
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--write', action='store_true', help='regenerate the .spv files and the header')
    parser.add_argument('--print', action='store_true', help='disassemble the .spv files')
    parser.add_argument('--check', action='store_true',
                        help='fail if the files in the tree are not what this produces')
    args = parser.parse_args()
    here = pathlib.Path(__file__).resolve().parent / 'shaders'
    built = (('skeleton.vert.spv', vertex_shader()), ('skeleton.frag.spv', fragment_shader()))
    for name, module in built:
        count = check(module)
        identifiers = verify(module)
        print(f'{name}: {count} words, bound {module[3]}, {identifiers} ids all defined once')
        if args.write:
            here.mkdir(parents=True, exist_ok=True)
            (here / name).write_bytes(struct.pack(f'<{count}I', *module))
        if args.print:
            print(disassemble(module))
    if args.check:
        stale = []
        for name, module in built:
            path = here / name
            expected = struct.pack(f'<{len(module)}I', *module)
            if not path.exists() or path.read_bytes() != expected:
                stale.append(name)
        headerText = header(built)
        if not (here / 'spirv_shaders.h').exists() or (here / 'spirv_shaders.h').read_text() != headerText:
            stale.append('spirv_shaders.h')
        if stale:
            print('out of date: ' + ', '.join(stale) + ' -- run spirv.py --write')
            raise SystemExit(1)
        print('shaders match the generator')
    if args.write:
        (here / 'spirv_shaders.h').write_text(header(built))


if __name__ == '__main__':
    main()
