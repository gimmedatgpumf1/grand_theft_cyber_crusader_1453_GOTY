"""Emit the original labelled anatomical graph for the C-room loader.

This is linkage and descriptive class data only. It contains no measured
joint frames, neutral poses, numerical constraints or kinematic solver.
"""
import ast
from pathlib import Path
from joint_mobility import mobility

HERE = Path(__file__).resolve().parent
source = ast.parse((HERE / '04-line-graph.py').read_text())
functions = [node for node in source.body if isinstance(node, ast.FunctionDef) and node.name == 'anatomy']
namespace = {}
exec(compile(ast.Module(body=functions, type_ignores=[]), 'anatomical graph', 'exec'), namespace)
edges = namespace['anatomy']()
rows = ['# Original OpenSkeleton-labelled relationships; not a kinematic rig.',
        '# kind first second anatomical_mobility_class']
for first, second, kind in edges:
    rows.append(f'{kind} {first} {second} {mobility(first, second, kind)}')
(HERE.parent / 'skeleton-joint-links.txt').write_text('\n'.join(rows)+'\n')
print(f'{len(edges)} source relationships written')
