# MakeHuman foot fitting — visual work in progress

![Full-resolution fit and post-fit reduction](progress.png)

This is **not yet the in-game foot**. Some blue bones remain exposed in bent
poses, the nails still need work, and ankle/ragdoll attachment is incomplete.
The existing procedural foot remains in the game. The images are CPU renders,
not Vulkan or gameplay captures.

`extract-makehuman-foot.py` cuts the **full-resolution** left foot from the
vendored CC0 MakeHuman body for each sex, applies the young-adult target, and
retains up to four original rig influences per vertex. `experimental-fit.h`
fits the complete source mesh to the comparison skeleton. An offline,
point-based clearance adjustment uses a temporarily capped ankle, then saves
canonical offsets. The temporary cap is not part of the output foot.

Only **after that fit**, `simplify-fitted-foot.py` reduces the fitted result and
records barycentric correspondence to the original mesh. The experimental
reduced visualizer samples the full-resolution profile fit and uses two of the
source influences for toe movement; the full-resolution visualizer uses a single
dominant influence. The reduced result is not yet integrated or GPU skinned.
Canonical corrections are approximate, not a guarantee of collision-free or
anatomically accurate skin at other profiles or poses. The former low-poly-first
asset has been removed; it remains in Git history.

## Reproducing the offline data

Install NumPy, SciPy, trimesh, rtree, networkx and fast-simplification in a
Python environment. Run from the repository root:

```
python3 tools/foot/extract-makehuman-foot.py
```

Compile `tools/foot/export-fit.cpp` against the native client objects and run
it from the repository root with an output directory. It exports **uncorrected**
full-resolution geometry and comparison bones for both sexes and sides. Then:

```
python3 tools/foot/fit-bone-clearance.py OUTPUT/m_L
python3 tools/foot/fit-bone-clearance.py OUTPUT/f_L
python3 tools/foot/simplify-fitted-foot.py OUTPUT/m_L
python3 tools/foot/simplify-fitted-foot.py OUTPUT/f_L
```

Replace `OUTPUT` with the exporter directory. These commands overwrite the
canonical correction and reduced topology headers in `tools/foot/`.
`render-progress.cpp` can be compiled against the native client objects and
run as `render-progress OUTPUT [--reduced]` to render both sexes, both sides,
rest/bent poses and four angles. Rebuild the visualizer after regenerating data.

The body source is `tools/human-source/` and its license is
`tools/human-source/CC0.txt`; see `NOTICE.md` for attribution.
