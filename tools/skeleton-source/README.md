# Skeleton source

`tools/skeleton-source/` holds the anatomical bodies the game and the test room
draw, plus the scripts that produce and check them.

| file | parts | bones | vertices | triangles | height |
| --- | --- | --- | --- | --- | --- |
| `skeleton-male.obj` | 28 | 272 | 97,227 | 195,242 | 1.696 m |
| `skeleton-female.obj` | 27 | 182 | 90,118 | 179,824 | 1.662 m |

The male is a real anatomical model (Z-Anatomy, CC BY-SA 4.0, derived from
BodyParts3D): every bone is present, including all eight carpals and every finger
phalanx, the ossicles, the teeth, and every toe.

The female is a **different body**: a 41-year-old woman from OpenSkeleton
(NMDID post-mortem CT, Ghent University), 170.2 cm and 78.0 kg, assembled by
`openskeleton/03-import.py` from her own 182 segmented bones. Her pelvis, her
rib cage, her skull, her hands and her feet are hers; nothing is a warp of the
male, and nothing is scaled. `openskeleton/README.md` has the measurements.

## Canonical height

Measured from the file, not assumed: the male is **1.6958 m** from the lowest
bone vertex to the crown, the female **1.7130 m**, both with the feet on `y = 0`.

Nothing in this repo scales either body. The pipeline applies a centimetre to
metre conversion and a translation that puts the feet on the floor, and that is
all. Bone height reads ~2–3 cm short of living stature because cartilage, discs
and the soft tissue of the scalp and heel pad are not bone.

A rig that needs a taller body gets a taller body, never a scaled one.

## Format

Units are metres, feet on `y = 0`, facing +z. Each of the 28 parts is an `o`
group named after the rig bone it hangs from (`skull`, `femur_l`, `hand_r`,
`ribs_l`, …), so the rig binds by name rather than by guessing from coordinates.

`skeleton-male.parts.json` carries the per-part triangle counts, the bounding box
and **one record per bone** — source name, part, side, centre and extent in
metres — which is what the finger and toe rig will be built from.

## Checking it

```bash
node tools/skeleton-source/verify-obj.mjs tools/skeleton-source/skeleton-male.obj
node tools/skeleton-source/verify-obj.mjs tools/skeleton-source/skeleton-female.obj \
    --sex female --scanned
```

`--sex` picks the osteometric series and `--scanned` says the body is one
individual rather than a constructed model, so a part that is out of band but
agrees with its own reflection is reported as that person's build instead of as
a fault. Both checks run in `tests/run_tests.sh`.

It parses only the shipped file, so it answers "is the thing we actually load
anatomically sane", not "does the exporter believe it copied correctly". It
reports:

* height, feet height, bounding box, vertex and triangle counts;
* long-bone length as a percentage of stature against adult reference bands
  (Trotter & Gleser ±2 SD), with the miss in millimetres where a part sits
  outside its band;
* left/right mirror error for every paired part, in millimetres.

A proportion outside its band is reported as `off` and tolerated up to 10 mm —
one real individual can sit outside a population range. A miss larger than that,
a missing part, or a mirror error over 3 mm is a `FAIL` and exits non-zero.

Current state: the male passes, with the foot 8.7 mm below the low end of its
band (a short foot on the source individual, not a deformation). The female
fails 12 checks, and the worst of them are structural rather than proportional:
her scapulae are 17.8 mm out of mirror, her humeri 13.6 mm.

## Regenerating

```bash
npm i meshoptimizer --prefix tools/skeleton-source
node tools/skeleton-source/prepare-zanatomy.mjs /path/to/SkeletalSystem100.fbx \
     tools/skeleton-source/skeleton-male.obj
```

`prepare.mjs` regenerates the female from a checkout of open-anatomy-3d;
`verify.mjs` re-derives that body's measurements from its source; `fbx.mjs` is a
dependency-free binary FBX reader and `measure-zanatomy.mjs` assembles a Z-Anatomy
FBX through its node transforms and prints osteometry.

`reference-*.png` are renders of the two bodies from this repo's own rasterizer,
front and three-quarter.
