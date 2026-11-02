#!/usr/bin/env python3
"""Embed the checked-in SPIR-V compute modules in the native client."""
from pathlib import Path
import struct

HERE = Path(__file__).resolve().parent
for name in ('rigid_solver', 'rigid_skin'):
    data = (HERE / (name + '.comp.spv')).read_bytes()
    if len(data) % 4:
        raise SystemExit(f'{name}: invalid SPIR-V length')
    words = struct.unpack(f'<{len(data) // 4}I', data)
    if not words or words[0] != 0x07230203:
        raise SystemExit(f'{name}: invalid SPIR-V magic')
    lines = [f'// Generated from {name}.comp.spv; do not edit.', '#pragma once',
             '#include <cstdint>', 'namespace gtc::native::gpu_physics {',
             f'inline constexpr std::uint32_t {name}_spirv[] = {{']
    words_per_line = 24 if name == 'rigid_solver' else 16
    for offset in range(0, len(words), words_per_line):
        lines.append('    ' + ', '.join(f'0x{x:08x}u' for x in words[offset:offset + words_per_line]) + ',')
    lines += ['};', '}', '']
    (HERE / (name + '_spirv.h')).write_text('\n'.join(lines))
