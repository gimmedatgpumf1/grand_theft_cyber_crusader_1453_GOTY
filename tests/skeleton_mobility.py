"""Classify every edge in the current graph; this is not a kinematic rig test."""
import ast
from collections import Counter
from pathlib import Path
import runpy

base = Path(__file__).resolve().parent.parent / 'tools/skeleton-source/openskeleton'
tree = ast.parse((base / '04-line-graph.py').read_text())
body = [n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'anatomy']
namespace = {}
exec(compile(ast.Module(body=body, type_ignores=[]), 'anatomy', 'exec'), namespace)
classify = runpy.run_path(str(base / 'joint_mobility.py'))['mobility']
counts = Counter(classify(a, b, kind) for a, b, kind in namespace['anatomy']())
assert sum(counts.values()) == 209 and len(counts) == 31
assert counts['finger_mcp_condyloid'] == counts['finger_pip_hinge'] == counts['finger_dip_hinge'] == 8
assert counts['thumb_mcp'] == counts['thumb_ip_hinge'] == counts['thumb_cmc_saddle'] == 2
assert counts['toe_ip_hinge'] == 18
print('Anatomical graph relationship classifications: PASS (209 edges; not a runtime rig)')
