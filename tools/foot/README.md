# MakeHuman foot extraction (work in progress)

`extract-makehuman-foot.py` extracts the left foot from the vendored CC0
MakeHuman body, applies each sex's young-adult target, cuts the ankle, reduces
the triangulated surface to approximately 900 triangles, and records the
nearest original rig influence. The right foot is reflected at runtime. The
source is `tools/human-source/` and its license is `tools/human-source/CC0.txt`;
see `NOTICE.md` for attribution.

To regenerate the checked-in data, install NumPy, SciPy and
`fast-simplification` in a Python environment, then run:

```
python3 tools/foot/extract-makehuman-foot.py
```

`experimental-fit.h` is an **unintegrated prototype**. Its joint-landmark fit
still exposes bones and misplaces the existing nails, especially during toe
flexion. It is preserved here for further work, not used by the game. The
existing procedural foot remains active until those issues are resolved.
