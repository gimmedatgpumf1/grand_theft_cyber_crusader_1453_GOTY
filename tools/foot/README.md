# MakeHuman foot fitting — visual work in progress

![Full-resolution fit and post-fit reduction](progress.png)

The blue skeletons in the test room currently show this **experimental**
MakeHuman replacement. The image above is the earlier CPU fitting comparison;
the room uses the reduced mesh with two-bone GPU blending. The 0.1 mm gap is
**not enforced at runtime** and is not guaranteed for arbitrary poses,
proportions or ragdoll motion. In a SwiftShader Vulkan room capture, ragdoll
motion exposed bone through the feet: this attachment is not finished.

The full-resolution CC0 MakeHuman mesh is fitted and attached to the blue bones
first; its original rig weights are used for two-influence toe articulation.
Only after that fit is the mesh reduced. The fitter uses real triangle-mesh
collision/distance queries (rather than only bone vertices) and checks that all
foot-bone components are inside the skin. Tibia and fibula are also checked
against the ankle surface, but are allowed to extend through its open rim. It
adjusts the reduced mesh again, because
simplification can reduce the gap. An ankle cap is used for containment checks
only, not added to the rendered foot. Corrections are stored for each sex and
mirrored for the other side.

The current offline pose set includes both sexes and sides, 50 cm, default and
250 cm heights, rest and simultaneous +20/-20-degree toe-base movements, and
individual +20/-20-degree toe joints at 50 cm. For each generated full-resolution
and reduced set, the minimum surface distance was above 0.1 mm with no bone/skin
intersections. This finite set is **not** proof for other poses, proportions,
ankle attachment or dynamic ragdolls. The MakeHuman foot influence is distributed
to nearby metatarsal, tarsal and ankle bones for GPU ragdoll preview, but this
approximate attachment and the provisional nails remain unfinished.

## Reproducing the offline data

Install NumPy, SciPy, trimesh, rtree, networkx, python-fcl and
fast-simplification. Compile `export-fit.cpp` and `export-pose.cpp` against the
native client objects (C++20) and run from the repository root. Replace the
uppercase names below with directories/files outside the repository:

1. Extract and fit full resolution at the default pose:

   ```
   python3 tools/foot/extract-makehuman-foot.py
   export-fit FIT
   python3 tools/foot/fit-bone-clearance.py FIT/m_L
   python3 tools/foot/fit-bone-clearance.py FIT/f_L
   ```

2. Recompile `export-pose` after regenerating headers. Export the fitted high
   mesh **without** prior pose offsets, fit the pose set, and write offsets:

   ```
   export-pose HIGH_POSES --base-only --sweep
   python3 tools/foot/fit-poses.py HIGH_POSES m MALE_BASIC --ignore-ankle --small-only --basic-only
   python3 tools/foot/fit-poses.py HIGH_POSES f FEMALE_BASIC --ignore-ankle --small-only --basic-only
   python3 tools/foot/fit-poses.py HIGH_POSES m MALE_TOES --ignore-ankle --small-only --initial MALE_BASIC
   python3 tools/foot/fit-poses.py HIGH_POSES f FEMALE_TOES --ignore-ankle --small-only --initial FEMALE_BASIC
   python3 tools/foot/fit-poses.py HIGH_POSES m MALE_HIGH_OFFSETS --initial MALE_TOES
   python3 tools/foot/fit-poses.py HIGH_POSES f FEMALE_HIGH_OFFSETS --initial FEMALE_TOES
   python3 tools/foot/apply-pose-correction.py FIT/m_L HIGH_POSES/m0_L-rest MALE_HIGH_OFFSETS
   python3 tools/foot/apply-pose-correction.py FIT/f_L HIGH_POSES/f0_L-rest FEMALE_HIGH_OFFSETS
   ```

3. Reduce those fitted meshes, compile `export-pose` again, and fit the reduced
   meshes with high-resolution pose offsets enabled but reduced offsets disabled:

   ```
   python3 tools/foot/simplify-fitted-foot.py FIT/m_L --fitted skin-pose-fitted.obj
   python3 tools/foot/simplify-fitted-foot.py FIT/f_L --fitted skin-pose-fitted.obj
   export-pose LOW_POSES --reduced --base-only --sweep
   python3 tools/foot/fit-poses.py LOW_POSES m MALE_LOW_OFFSETS
   python3 tools/foot/fit-poses.py LOW_POSES f FEMALE_LOW_OFFSETS
   python3 tools/foot/apply-reduced-pose-correction.py LOW_POSES/m0_L-rest MALE_LOW_OFFSETS
   python3 tools/foot/apply-reduced-pose-correction.py LOW_POSES/f0_L-rest FEMALE_LOW_OFFSETS
   ```

4. Recompile the exporter and verify **fresh** exports with all offsets applied:

   ```
   export-pose HIGH_FINAL --sweep
   export-pose LOW_FINAL --reduced --sweep
   python3 tools/foot/check-clearance.py HIGH_FINAL
   python3 tools/foot/check-clearance.py LOW_FINAL
   ```

`render-progress.cpp` renders both sexes, both sides, rest/bent poses and four
angles; run `render-progress OUTPUT [--reduced]` after compiling. The body source
is `tools/human-source/`, its license is `tools/human-source/CC0.txt`, and
`NOTICE.md` contains attribution.
