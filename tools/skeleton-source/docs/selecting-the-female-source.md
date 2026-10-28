# Which female skeleton, and why

A record of the search, so the next person does not repeat it. The question was
whether a better female skeleton exists than the Visible Human Female. Below is
every open or purchasable full-body female skeleton found, and why it is or is
not usable.

## The field

| source | subject | coverage | licence / cost | verdict |
| --- | --- | --- | --- | --- |
| Visible Human Female (NLM, via Comodo) | 59 y woman, 157 cm, 88 kg | complete | public domain / CC BY 4.0 | **shipped today** — 1995 CT, 1 mm slices |
| BodyParts3D 4.0 | adult male | complete | CC BY 4.0 | male |
| Z-Anatomy | adult male | complete | CC BY-SA 4.0 | male |
| Open 3D Model (anatomytool) | male | complete skeleton | CC BY-SA | male |
| HRA / HuBMAP 3D reference, female | the same Visible Human Female | pelvis, vertebrae, sternum, legs, organs — **no skull, ribs, arms, feet** | CC BY 4.0 | partial |
| DU Visible Human library | both VH subjects | 28 bones per subject, **legs only**, from 0.33 mm cryosections | CC BY 4.0 | partial |
| Sreenivasa & Gonzalez-Alvarado 2018 | the same Visible Human Female | lower body bones only | open | partial |
| BoneHub Visible Human 3D | both VH subjects | **143 female bones**, human-verified, every carpal and metatarsal separate | CC BY 4.0 | strong, but phalanges grouped per digit and sternum undivided |
| Zygote female skeleton | — | complete, 1,072,684 triangles, hand-modelled from CT | **$4,260** | paid |
| **OpenSkeleton** | **51 female NMDID cadavers** | **182 structures, complete skeleton** | **CC BY 4.0** | **chosen** |

Every open *full* female skeleton in that table is the same 1995 cadaver. The
only way to a different woman is OpenSkeleton.

## Why BoneHub is not the base either

BoneHub is a genuine improvement — two trained annotators with a promptable
network, 143 individual labels — and it is reachable, because its meshes live in
`BoneHub/Educational-Atlas` on GitHub rather than only behind Hugging Face. Three
of its documented limits disqualify it as the base mesh:

- **Phalanges are grouped one label per digit.** The interphalangeal joints do
  not exist as separate geometry, and 27 bones per hand is an acceptance
  criterion here.
- **The sternum is one label**, so the manubrium and body cannot articulate.
- **The hyoid and the sesamoids are absent.**

Its "incomplete elbow" warning, however, is about their *aligned* copy of the CT,
not about the cadaver: the full-body series in use here has no gaps at the elbow.
Checked directly — no duplicate slices, no bone-free runs below the crown.

BoneHub remains useful as an **independent audit** of a scanned build, since it
labels carpals, metacarpals, tarsals and metatarsals individually and was made by
different hands than this pipeline.

## Why OpenSkeleton

- 51 women, so the subject can be chosen for stature and habitus instead of
  inherited.
- Modern thin-slice CT, so thin plates and phalanges resolve.
- 182 structures, complete, and consistent across subjects.
- CC BY 4.0.

Two consequences follow, and both are already rules in this repository:

1. **Her height is hers.** The subject nearest 1.70 m is chosen up front, and
   nothing is ever scaled afterwards.
2. **Nothing from the old body is carried over silently.** The male and the
   Visible Human Female stay in the room until the new body passes the verifier,
   the shell census and the test-room render.
